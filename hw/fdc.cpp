// Computron x86 PC Emulator
// Copyright (C) 2003-2018 Andreas Kling <awesomekling@gmail.com>
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY ANDREAS KLING ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ANDREAS KLING OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
// OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "fdc.h"
#include "Common.h"
#include "DiskDrive.h"
#include "debug.h"
#include "machine.h"
#include "pic.h"

#define FDC_NEC765
#define FDC_DEBUG

// Spec: http://www.buchty.net/casio/files/82077.pdf

// 0x3f4 - MSR (Main Status Register)
#define FDC_MSR_RQM (1 << 7)
#define FDC_MSR_DIO (1 << 6)
#define FDC_MSR_NONDMA (1 << 5)
#define FDC_MSR_CMDBSY (1 << 4)
#define FDC_MSR_DRV3BSY (1 << 3)
#define FDC_MSR_DRV2BSY (1 << 2)
#define FDC_MSR_DRV1BSY (1 << 1)
#define FDC_MSR_DRV0BSY (1 << 0)

#define DATA_REGISTER_READY 0x80

enum FDCCommand {
    SenseInterruptStatus = 0x08,
    SpecifyStepAndHeadLoad = 0x03,
    SeekToTrack = 0x0f,
    Recalibrate = 0x07,
    GetVersion = 0x10,
    DumpRegisters = 0x0e,
    PerpendicularMode = 0x12,
    Configure = 0x13,
    Lock = 0x94,
    Unlock = 0x14,
    SenseDriveStatus = 0x04,
};

enum FDCDataRate {
    _500kbps = 0,
    _300kbps = 1,
    _250kbps = 2,
    _1000kbps = 3,
};

static const char* to_string(FDCDataRate rate)
{
    switch (rate) {
    case _500kbps:
        return "500 kbps";
    case _300kbps:
        return "300 kbps";
    case _250kbps:
        return "250 kbps";
    case _1000kbps:
        return "1000 kbps";
    }
    ASSERT_NOT_REACHED();
    return nullptr;
}

struct FDCDrive {
    FDCDrive() { }

    bool motor { false };
    u8 cylinder { 0 };
    u8 head { 0 };
    u8 sector { 0 };
    u8 step_rate_time { 0 };
    u8 head_load_time { 0 };
    u8 head_unload_time { 0 };
    u8 bytes_per_sector { 0 };
    u8 end_of_track { 0 };
    u8 gap3_length { 0 };
    u8 data_length { 0 };
    u8 digital_input_register { 0 };
};

struct FDC::Private {
    FDCDrive drive[2];
    u8 drive_index;
    bool enabled;
    FDCDataRate data_rate;
    u8 data_direction;
    u8 main_status_register;
    u8 status_register[4];
    bool has_pending_reset { false };
    QVector<u8> command;
    u8 command_size;
    QList<u8> command_result;
    u8 configure_data { 0 };
    u8 precompensation_start_number { 0 };
    u8 perpendicular_mode_config { 0 };
    bool lock { false };
    u8 expected_sense_interrupt_count { 0 };

    FDCDrive& current_drive()
    {
        ASSERT(drive_index < 2);
        return drive[drive_index];
    }
};

FDC::FDC(Machine& machine)
    : IODevice("FDC", machine, 6)
    , d(make<Private>())
{
    listen(0x3F0, IODevice::ReadOnly);
    listen(0x3F1, IODevice::ReadOnly);
    listen(0x3F2, IODevice::WriteOnly);
    listen(0x3F4, IODevice::ReadWrite);
    listen(0x3F5, IODevice::ReadWrite);
    listen(0x3F7, IODevice::ReadWrite);

    reset();
}

FDC::~FDC()
{
}

void FDC::set_data_direction(DataDirection direction)
{
    if (direction == DataDirection::FromFDC)
        d->main_status_register |= (unsigned)DataDirection::FromFDC;
    else
        d->main_status_register &= ~(unsigned)DataDirection::FromFDC;
}

FDC::DataDirection FDC::data_direction() const
{
    return static_cast<DataDirection>(d->main_status_register & (unsigned)DataDirection::Mask);
}

void FDC::set_using_dma(bool value)
{
    if (value)
        d->main_status_register &= ~FDC_MSR_NONDMA;
    else
        d->main_status_register |= FDC_MSR_NONDMA;
}

bool FDC::using_dma() const
{
    return !(d->main_status_register & FDC_MSR_NONDMA);
}

void FDC::reset_controller(ResetSource reset_source)
{
    if (reset_source == Software) {
        vlog(LogFDC, "Reset by software");
    } else {
        d->data_rate = FDCDataRate::_250kbps;
        d->lock = false;

        // FIXME: I think we should pretend the disks are changed.
        // However I'm not sure when exactly to mark them as unchanged. Need to figure out.
        //d->drive[0].digitalInputRegister = 0x80;
        //d->drive[1].digitalInputRegister = 0x80;
    }

    d->has_pending_reset = false;
    d->drive_index = 0;
    d->enabled = false;
    set_using_dma(false);
    set_data_direction(DataDirection::ToFDC);
    d->main_status_register = 0;

    d->command_size = 0;
    d->command.clear();

    d->status_register[0] = 0;
    d->status_register[1] = 0;
    d->status_register[2] = 0;
    d->status_register[3] = 0x28;

    for (unsigned i = 0; i < 2; ++i) {
        d->drive[i].cylinder = 0;
        d->drive[i].head = 0;
        d->drive[i].sector = 0;
        d->drive[i].end_of_track = 0;
    }

    d->perpendicular_mode_config = 0;

    if (!d->lock) {
        d->configure_data = 0;
        d->precompensation_start_number = 0;
    }

    lower_irq();
}

void FDC::reset()
{
    reset_controller(ResetSource::Hardware);
}

u8 FDC::in8(u16 port)
{
    u8 data = 0;
    switch (port) {
    case 0x3F0: {
        if (machine().floppy1().present()) {
            /* Second drive installed */
            data |= 0x40;
        }
        if (is_irq_raised()) {
            data |= 0x80;
        }
        vlog(LogFDC, "Read status register A: %02X", data);
        break;
    }
    case 0x3F1:
        vlog(LogFDC, "Read status register B: (FIXME)");
        // FIXME: What should this register contain?
        break;

    case 0x3F4: {
        vlog(LogFDC, "Read main status register: %02x (direction: %s)", d->main_status_register, (data_direction() == DataDirection::ToFDC) ? "to FDC" : "from FDC");
        return d->main_status_register;
    }

    case 0x3F5: {
        if (d->command_result.isEmpty()) {
            vlog(LogFDC, "Read from empty command result register");
            return IODevice::JunkValue;
        }

        data = d->command_result.takeFirst();
        vlog(LogFDC, "Read command result byte %02X", data);

        if (d->command_result.isEmpty()) {
            set_data_direction(DataDirection::ToFDC);
        }

        break;

    case 0x3F7:
        if (d->drive_index < 2) {
            vlog(LogFDC, "Read drive %u DIR = %02X", d->drive_index, d->current_drive().digital_input_register);
            data = d->current_drive().digital_input_register;
        } else
            vlog(LogFDC, "Wanted DIR, but invalid drive %u selected", d->drive_index);
        break;
    }

    default:
        ASSERT_NOT_REACHED();
        IODevice::in8(port);
    }
#ifdef FDC_DEBUG
    vlog(LogFDC, " in8 %03x = %02x", port, data);
#endif
    return data;
}

static bool isReadDataCommand(u8 b)
{
    return (b & 0x1f) == 0x06;
}

void FDC::out8(u16 port, u8 data)
{
#ifdef FDC_DEBUG
    vlog(LogFDC, "out8 %03x, %02x", port, data);
#endif
    switch (port) {
    case 0x3F2: {
        bool was_enabled = d->enabled;

        vlog(LogFDC, "Writing to FDC digital output, data: %02X", data);

        d->drive_index = data & 3;
        d->enabled = (data & 0x04) != 0;
        set_using_dma(data & 0x08);

        d->drive[0].motor = (data & 0x10) != 0;
        d->drive[1].motor = (data & 0x20) != 0;

        vlog(LogFDC, "  Current drive: %u", d->drive_index);
        vlog(LogFDC, "  FDC enabled:   %s", d->enabled ? "yes" : "no");
        vlog(LogFDC, "  DMA+I/O mode:  %s", using_dma() ? "yes" : "no");

        vlog(LogFDC, "  Motors:        %u %u", d->drive[0].motor, d->drive[1].motor);

        //if (!d->current_drive().motor)
        //    vlog(LogFDC, "Invalid state: Current drive (%u) has motor off.", d->drive_index);

        if (!was_enabled && d->enabled) {
            // Back to business.
        } else if (was_enabled && !d->enabled) {
            reset_controller_soon();
        }

        break;
    }

    case 0x3F5: {
        vlog(LogFDC, "Command byte: %02X", data);

        if (d->command.isEmpty()) {
            d->main_status_register &= FDC_MSR_DIO;
            d->main_status_register |= FDC_MSR_RQM | FDC_MSR_CMDBSY;
            // Determine the command length
            if (isReadDataCommand(data)) {
                d->command_size = 9;
            } else {
                switch (data) {
                case GetVersion:
                case SenseInterruptStatus:
                case DumpRegisters:
                case Lock:
                case Unlock:
                    d->command_size = 1;
                    break;
                case Recalibrate:
                case PerpendicularMode:
                case SenseDriveStatus:
                    d->command_size = 2;
                    break;
                case SeekToTrack:
                case SpecifyStepAndHeadLoad:
                    d->command_size = 3;
                    break;
                case Configure:
                    d->command_size = 4;
                    break;
                }
            }
        }

        d->command.append(data);

        if (d->command.size() >= d->command_size) {
            execute_command_soon();
        }
        break;
    }

    case 0x3f4:
        d->data_rate = static_cast<FDCDataRate>(data & 3);
        vlog(LogFDC, "Set data rate (via Data Rate Select Register): %s", to_string(d->data_rate));
        if (data & 0x80) {
            reset_controller_soon();
        }
        if (data & 0x40) {
            // Power down
            ASSERT_NOT_REACHED();
        }
        break;

    case 0x3f7:
        d->data_rate = static_cast<FDCDataRate>(data & 3);
        vlog(LogFDC, "Set data rate (via Configuration Control Register): %s", to_string(d->data_rate));
        break;

    default:
        IODevice::out8(port, data);
    }
}

void FDC::reset_controller_soon()
{
    d->has_pending_reset = true;
    d->main_status_register &= FDC_MSR_NONDMA;
    execute_command_soon();
}

void FDC::execute_read_data_command()
{
    d->drive_index = d->command[1] & 3;
    d->current_drive().head = (d->command[1] >> 2) & 1;
    d->current_drive().cylinder = d->command[2];
    d->current_drive().head = d->command[3];
    d->current_drive().sector = d->command[4];
    d->current_drive().bytes_per_sector = d->command[5];
    d->current_drive().end_of_track = d->command[6];
    d->current_drive().gap3_length = d->command[7];
    d->current_drive().data_length = d->command[8];
    vlog(LogFDC, "ReadData { drive:%u, C:%u H:%u, S:%u / bpS:%u, EOT:%u, g3l:%u, dl:%u }",
        d->drive_index,
        d->current_drive().cylinder,
        d->current_drive().head,
        d->current_drive().sector,
        128 << d->current_drive().bytes_per_sector,
        d->current_drive().end_of_track,
        d->current_drive().gap3_length,
        d->current_drive().data_length);
}

void FDC::execute_command_soon()
{
    // FIXME: Don't do this immediately, do it "soon"!
    execute_command();
}

void FDC::execute_command()
{
    execute_command_internal();
    d->command.clear();
    if ((d->status_register[0] & 0xc0) == 0x80) {
        // Command was invalid
        d->command_result.clear();
        d->command_result.append(d->status_register[0]);
    }
    set_data_direction(!d->command_result.isEmpty() ? DataDirection::FromFDC : DataDirection::ToFDC);
    d->main_status_register |= FDC_MSR_RQM;

    if (d->command_result.isEmpty()) {
        d->main_status_register &= ~FDC_MSR_CMDBSY;
    } else {
        d->main_status_register |= FDC_MSR_CMDBSY;
    }
}

void FDC::execute_command_internal()
{
    if (d->has_pending_reset) {
        reset_controller(Software);
        d->expected_sense_interrupt_count = 4;
        generate_fdc_interrupt();
        return;
    }

    vlog(LogFDC, "Executing command %02x", d->command[0]);
    d->command_result.clear();

    if (isReadDataCommand(d->command[0]))
        return execute_read_data_command();

    switch (d->command[0]) {
    case SpecifyStepAndHeadLoad:
        d->current_drive().step_rate_time = (d->command[1] >> 4) & 0xf;
        d->current_drive().head_unload_time = d->command[1] & 0xf;
        d->current_drive().head_load_time = (d->command[2] >> 1) & 0x7f;

        set_using_dma(!(d->command[2] & 1));
        vlog(LogFDC, "SpecifyStepAndHeadLoad { SRT:%1x, HUT:%1x, HLT:%1x, ND:%1x }",
            d->current_drive().step_rate_time,
            d->current_drive().head_unload_time,
            d->current_drive().head_load_time,
            !using_dma());
        break;
    case SenseInterruptStatus:
        vlog(LogFDC, "SenseInterruptStatus");
        d->command_result.append(d->status_register[0]);
        d->command_result.append(d->current_drive().cylinder);
        // Linux sends 4 SenseInterruptStatus commands after a controller reset because of "drive polling"
        if (d->expected_sense_interrupt_count) {
            u8 driveIndex = 4 - d->expected_sense_interrupt_count;
            d->status_register[0] &= 0xf8;
            d->status_register[0] |= (d->drive[driveIndex].head << 2) | driveIndex;
            --d->expected_sense_interrupt_count;
        } else if (!is_irq_raised()) {
            d->status_register[0] = 0x80;
        }
        break;
    case Recalibrate:
        d->drive_index = d->command[1] & 3;
        generate_fdc_interrupt();
        vlog(LogFDC, "Recalibrate { drive:%u }", d->drive_index);
        break;
    case SeekToTrack:
        d->drive_index = d->command[1] & 3;
        d->current_drive().head = (d->command[1] >> 2) & 1;
        d->current_drive().cylinder = d->command[2];
        vlog(LogFDC, "SeekToTrack { drive:%u, C:%u, H:%u }",
            d->drive_index,
            d->current_drive().cylinder,
            d->current_drive().head);
        generate_fdc_interrupt(true);
        break;
    case GetVersion:
        vlog(LogFDC, "Get version");
#ifdef FDC_NEC765
        d->command_result.append(0x80);
#else
        update_status();
        d->command_result.append(d->status_register[0]);
#endif
        break;
    case DumpRegisters:
        d->command_result.append(d->drive[0].cylinder);
        d->command_result.append(d->drive[1].cylinder);
        d->command_result.append(0); // Drive 2 cylinder
        d->command_result.append(0); // Drive 3 cylinder
        d->command_result.append((d->current_drive().step_rate_time << 4) | (d->current_drive().head_unload_time));
        d->command_result.append((d->current_drive().head_unload_time << 1) | !using_dma());
        d->command_result.append((d->current_drive().end_of_track << 1) | !using_dma());
        d->command_result.append((d->lock * 0x80) | (d->perpendicular_mode_config & 0x7f));
        d->command_result.append(d->configure_data);
        d->command_result.append(d->precompensation_start_number);
        break;
    case PerpendicularMode:
        d->perpendicular_mode_config = d->command[1];
        vlog(LogFDC, "Perpendicular mode configuration: %02x", d->perpendicular_mode_config);
        break;
    case Lock:
    case Unlock:
        d->lock = (d->command[0] & 0x80);
        d->command_result.append(d->lock * 0x10);
        break;
    case Configure:
        if (d->command[1] != 0) {
            vlog(LogFDC, "Weird, expected second byte of Configure command to be all zeroes!");
        }
        d->configure_data = d->command[2];
        d->precompensation_start_number = d->command[3];
        break;
    case SenseDriveStatus: {
        u8 driveIndex = d->command[1] & 3;
        d->drive[driveIndex].head = (d->command[1] >> 2) & 1;
        d->status_register[3] = 0x28; // Reserved bits, always set.
        d->status_register[3] |= d->command[1] & 7;
        if (d->drive[driveIndex].cylinder == 0)
            d->status_register[3] |= 0x10;
        d->command_result.append(d->status_register[3]);
        break;
    }
    default:
        vlog(LogFDC, "Unknown command! %02X", d->command[0]);
        if (d->command[0] != 0x18)
            ASSERT_NOT_REACHED();
        d->status_register[0] = 0x80;
        break;
    }
}

void FDC::update_status(bool seekCompleted)
{
    d->status_register[0] = d->drive_index;
    d->status_register[0] |= d->current_drive().head * 0x02;

    if (seekCompleted)
        d->status_register[0] |= 0x20;
}

void FDC::generate_fdc_interrupt(bool seek_completed)
{
    update_status(seek_completed);
    vlog(LogFDC, "Raise IRQ%s", seek_completed ? " (seek completed)" : "");
    raise_irq();
}
