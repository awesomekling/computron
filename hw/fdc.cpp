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

static const char* toString(FDCDataRate rate)
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
    BYTE cylinder { 0 };
    BYTE head { 0 };
    BYTE sector { 0 };
    BYTE stepRateTime { 0 };
    BYTE headLoadTime { 0 };
    BYTE headUnloadTime { 0 };
    BYTE bytesPerSector { 0 };
    BYTE endOfTrack { 0 };
    BYTE gap3Length { 0 };
    BYTE dataLength { 0 };
    BYTE digitalInputRegister { 0 };
};

struct FDC::Private {
    FDCDrive drive[2];
    BYTE driveIndex;
    bool enabled;
    FDCDataRate dataRate;
    BYTE dataDirection;
    BYTE mainStatusRegister;
    BYTE statusRegister[4];
    bool hasPendingReset { false };
    QVector<BYTE> command;
    BYTE commandSize;
    QList<BYTE> commandResult;
    BYTE configureData { 0 };
    BYTE precompensationStartNumber { 0 };
    BYTE perpendicularModeConfig { 0 };
    bool lock { false };
    BYTE expectedSenseInterruptCount { 0 };

    FDCDrive& currentDrive()
    {
        ASSERT(driveIndex < 2);
        return drive[driveIndex];
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

void FDC::setDataDirection(DataDirection direction)
{
    if (direction == DataDirection::FromFDC)
        d->mainStatusRegister |= (unsigned)DataDirection::FromFDC;
    else
        d->mainStatusRegister &= ~(unsigned)DataDirection::FromFDC;
}

FDC::DataDirection FDC::dataDirection() const
{
    return static_cast<DataDirection>(d->mainStatusRegister & (unsigned)DataDirection::Mask);
}

void FDC::setUsingDMA(bool value)
{
    if (value)
        d->mainStatusRegister &= ~FDC_MSR_NONDMA;
    else
        d->mainStatusRegister |= FDC_MSR_NONDMA;
}

bool FDC::usingDMA() const
{
    return !(d->mainStatusRegister & FDC_MSR_NONDMA);
}

void FDC::resetController(ResetSource resetSource)
{
    if (resetSource == Software) {
        vlog(LogFDC, "Reset by software");
    } else {
        d->dataRate = FDCDataRate::_250kbps;
        d->lock = false;

        // FIXME: I think we should pretend the disks are changed.
        // However I'm not sure when exactly to mark them as unchanged. Need to figure out.
        //d->drive[0].digitalInputRegister = 0x80;
        //d->drive[1].digitalInputRegister = 0x80;
    }

    d->hasPendingReset = false;
    d->driveIndex = 0;
    d->enabled = false;
    setUsingDMA(false);
    setDataDirection(DataDirection::ToFDC);
    d->mainStatusRegister = 0;

    d->commandSize = 0;
    d->command.clear();

    d->statusRegister[0] = 0;
    d->statusRegister[1] = 0;
    d->statusRegister[2] = 0;
    d->statusRegister[3] = 0x28;

    for (unsigned i = 0; i < 2; ++i) {
        d->drive[i].cylinder = 0;
        d->drive[i].head = 0;
        d->drive[i].sector = 0;
        d->drive[i].endOfTrack = 0;
    }

    d->perpendicularModeConfig = 0;

    if (!d->lock) {
        d->configureData = 0;
        d->precompensationStartNumber = 0;
    }

    lowerIRQ();
}

void FDC::reset()
{
    resetController(ResetSource::Hardware);
}

BYTE FDC::in8(WORD port)
{
    BYTE data = 0;
    switch (port) {
    case 0x3F0: {
        if (machine().floppy1().present()) {
            /* Second drive installed */
            data |= 0x40;
        }
        if (isIRQRaised()) {
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
        vlog(LogFDC, "Read main status register: %02x (direction: %s)", d->mainStatusRegister, (dataDirection() == DataDirection::ToFDC) ? "to FDC" : "from FDC");
        return d->mainStatusRegister;
    }

    case 0x3F5: {
        if (d->commandResult.isEmpty()) {
            vlog(LogFDC, "Read from empty command result register");
            return IODevice::JunkValue;
        }

        data = d->commandResult.takeFirst();
        vlog(LogFDC, "Read command result byte %02X", data);

        if (d->commandResult.isEmpty()) {
            setDataDirection(DataDirection::ToFDC);
        }

        break;

    case 0x3F7:
        if (d->driveIndex < 2) {
            vlog(LogFDC, "Read drive %u DIR = %02X", d->driveIndex, d->currentDrive().digitalInputRegister);
            data = d->currentDrive().digitalInputRegister;
        } else
            vlog(LogFDC, "Wanted DIR, but invalid drive %u selected", d->driveIndex);
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

static bool isReadDataCommand(BYTE b)
{
    return (b & 0x1f) == 0x06;
}

void FDC::out8(WORD port, BYTE data)
{
#ifdef FDC_DEBUG
    vlog(LogFDC, "out8 %03x, %02x", port, data);
#endif
    switch (port) {
    case 0x3F2: {
        bool wasEnabled = d->enabled;

        vlog(LogFDC, "Writing to FDC digital output, data: %02X", data);

        d->driveIndex = data & 3;
        d->enabled = (data & 0x04) != 0;
        setUsingDMA(data & 0x08);

        d->drive[0].motor = (data & 0x10) != 0;
        d->drive[1].motor = (data & 0x20) != 0;

        vlog(LogFDC, "  Current drive: %u", d->driveIndex);
        vlog(LogFDC, "  FDC enabled:   %s", d->enabled ? "yes" : "no");
        vlog(LogFDC, "  DMA+I/O mode:  %s", usingDMA() ? "yes" : "no");

        vlog(LogFDC, "  Motors:        %u %u", d->drive[0].motor, d->drive[1].motor);

        //if (!d->currentDrive().motor)
        //    vlog(LogFDC, "Invalid state: Current drive (%u) has motor off.", d->driveIndex);

        if (!wasEnabled && d->enabled) {
            // Back to business.
        } else if (wasEnabled && !d->enabled) {
            resetControllerSoon();
        }

        break;
    }

    case 0x3F5: {
        vlog(LogFDC, "Command byte: %02X", data);

        if (d->command.isEmpty()) {
            d->mainStatusRegister &= FDC_MSR_DIO;
            d->mainStatusRegister |= FDC_MSR_RQM | FDC_MSR_CMDBSY;
            // Determine the command length
            if (isReadDataCommand(data)) {
                d->commandSize = 9;
            } else {
                switch (data) {
                case GetVersion:
                case SenseInterruptStatus:
                case DumpRegisters:
                case Lock:
                case Unlock:
                    d->commandSize = 1;
                    break;
                case Recalibrate:
                case PerpendicularMode:
                case SenseDriveStatus:
                    d->commandSize = 2;
                    break;
                case SeekToTrack:
                case SpecifyStepAndHeadLoad:
                    d->commandSize = 3;
                    break;
                case Configure:
                    d->commandSize = 4;
                    break;
                }
            }
        }

        d->command.append(data);

        if (d->command.size() >= d->commandSize) {
            executeCommandSoon();
        }
        break;
    }

    case 0x3f4:
        d->dataRate = static_cast<FDCDataRate>(data & 3);
        vlog(LogFDC, "Set data rate (via Data Rate Select Register): %s", toString(d->dataRate));
        if (data & 0x80) {
            resetControllerSoon();
        }
        if (data & 0x40) {
            // Power down
            ASSERT_NOT_REACHED();
        }
        break;

    case 0x3f7:
        d->dataRate = static_cast<FDCDataRate>(data & 3);
        vlog(LogFDC, "Set data rate (via Configuration Control Register): %s", toString(d->dataRate));
        break;

    default:
        IODevice::out8(port, data);
    }
}

void FDC::resetControllerSoon()
{
    d->hasPendingReset = true;
    d->mainStatusRegister &= FDC_MSR_NONDMA;
    executeCommandSoon();
}

void FDC::executeReadDataCommand()
{
    d->driveIndex = d->command[1] & 3;
    d->currentDrive().head = (d->command[1] >> 2) & 1;
    d->currentDrive().cylinder = d->command[2];
    d->currentDrive().head = d->command[3];
    d->currentDrive().sector = d->command[4];
    d->currentDrive().bytesPerSector = d->command[5];
    d->currentDrive().endOfTrack = d->command[6];
    d->currentDrive().gap3Length = d->command[7];
    d->currentDrive().dataLength = d->command[8];
    vlog(LogFDC, "ReadData { drive:%u, C:%u H:%u, S:%u / bpS:%u, EOT:%u, g3l:%u, dl:%u }",
        d->driveIndex,
        d->currentDrive().cylinder,
        d->currentDrive().head,
        d->currentDrive().sector,
        128 << d->currentDrive().bytesPerSector,
        d->currentDrive().endOfTrack,
        d->currentDrive().gap3Length,
        d->currentDrive().dataLength);
}

void FDC::executeCommandSoon()
{
    // FIXME: Don't do this immediately, do it "soon"!
    executeCommand();
}

void FDC::executeCommand()
{
    executeCommandInternal();
    d->command.clear();
    if ((d->statusRegister[0] & 0xc0) == 0x80) {
        // Command was invalid
        d->commandResult.clear();
        d->commandResult.append(d->statusRegister[0]);
    }
    setDataDirection(!d->commandResult.isEmpty() ? DataDirection::FromFDC : DataDirection::ToFDC);
    d->mainStatusRegister |= FDC_MSR_RQM;

    if (d->commandResult.isEmpty()) {
        d->mainStatusRegister &= ~FDC_MSR_CMDBSY;
    } else {
        d->mainStatusRegister |= FDC_MSR_CMDBSY;
    }
}

void FDC::executeCommandInternal()
{
    if (d->hasPendingReset) {
        resetController(Software);
        d->expectedSenseInterruptCount = 4;
        generateFDCInterrupt();
        return;
    }

    vlog(LogFDC, "Executing command %02x", d->command[0]);
    d->commandResult.clear();

    if (isReadDataCommand(d->command[0]))
        return executeReadDataCommand();

    switch (d->command[0]) {
    case SpecifyStepAndHeadLoad:
        d->currentDrive().stepRateTime = (d->command[1] >> 4) & 0xf;
        d->currentDrive().headUnloadTime = d->command[1] & 0xf;
        d->currentDrive().headLoadTime = (d->command[2] >> 1) & 0x7f;

        setUsingDMA(!(d->command[2] & 1));
        vlog(LogFDC, "SpecifyStepAndHeadLoad { SRT:%1x, HUT:%1x, HLT:%1x, ND:%1x }",
            d->currentDrive().stepRateTime,
            d->currentDrive().headUnloadTime,
            d->currentDrive().headLoadTime,
            !usingDMA());
        break;
    case SenseInterruptStatus:
        vlog(LogFDC, "SenseInterruptStatus");
        d->commandResult.append(d->statusRegister[0]);
        d->commandResult.append(d->currentDrive().cylinder);
        // Linux sends 4 SenseInterruptStatus commands after a controller reset because of "drive polling"
        if (d->expectedSenseInterruptCount) {
            BYTE driveIndex = 4 - d->expectedSenseInterruptCount;
            d->statusRegister[0] &= 0xf8;
            d->statusRegister[0] |= (d->drive[driveIndex].head << 2) | driveIndex;
            --d->expectedSenseInterruptCount;
        } else if (!isIRQRaised()) {
            d->statusRegister[0] = 0x80;
        }
        break;
    case Recalibrate:
        d->driveIndex = d->command[1] & 3;
        generateFDCInterrupt();
        vlog(LogFDC, "Recalibrate { drive:%u }", d->driveIndex);
        break;
    case SeekToTrack:
        d->driveIndex = d->command[1] & 3;
        d->currentDrive().head = (d->command[1] >> 2) & 1;
        d->currentDrive().cylinder = d->command[2];
        vlog(LogFDC, "SeekToTrack { drive:%u, C:%u, H:%u }",
            d->driveIndex,
            d->currentDrive().cylinder,
            d->currentDrive().head);
        generateFDCInterrupt(true);
        break;
    case GetVersion:
        vlog(LogFDC, "Get version");
#ifdef FDC_NEC765
        d->commandResult.append(0x80);
#else
        updateStatus();
        d->commandResult.append(d->statusRegister[0]);
#endif
        break;
    case DumpRegisters:
        d->commandResult.append(d->drive[0].cylinder);
        d->commandResult.append(d->drive[1].cylinder);
        d->commandResult.append(0); // Drive 2 cylinder
        d->commandResult.append(0); // Drive 3 cylinder
        d->commandResult.append((d->currentDrive().stepRateTime << 4) | (d->currentDrive().headUnloadTime));
        d->commandResult.append((d->currentDrive().headUnloadTime << 1) | !usingDMA());
        d->commandResult.append((d->currentDrive().endOfTrack << 1) | !usingDMA());
        d->commandResult.append((d->lock * 0x80) | (d->perpendicularModeConfig & 0x7f));
        d->commandResult.append(d->configureData);
        d->commandResult.append(d->precompensationStartNumber);
        break;
    case PerpendicularMode:
        d->perpendicularModeConfig = d->command[1];
        vlog(LogFDC, "Perpendicular mode configuration: %02x", d->perpendicularModeConfig);
        break;
    case Lock:
    case Unlock:
        d->lock = (d->command[0] & 0x80);
        d->commandResult.append(d->lock * 0x10);
        break;
    case Configure:
        if (d->command[1] != 0) {
            vlog(LogFDC, "Weird, expected second byte of Configure command to be all zeroes!");
        }
        d->configureData = d->command[2];
        d->precompensationStartNumber = d->command[3];
        break;
    case SenseDriveStatus: {
        BYTE driveIndex = d->command[1] & 3;
        d->drive[driveIndex].head = (d->command[1] >> 2) & 1;
        d->statusRegister[3] = 0x28; // Reserved bits, always set.
        d->statusRegister[3] |= d->command[1] & 7;
        if (d->drive[driveIndex].cylinder == 0)
            d->statusRegister[3] |= 0x10;
        d->commandResult.append(d->statusRegister[3]);
        break;
    }
    default:
        vlog(LogFDC, "Unknown command! %02X", d->command[0]);
        if (d->command[0] != 0x18)
            ASSERT_NOT_REACHED();
        d->statusRegister[0] = 0x80;
        break;
    }
}

void FDC::updateStatus(bool seekCompleted)
{
    d->statusRegister[0] = d->driveIndex;
    d->statusRegister[0] |= d->currentDrive().head * 0x02;

    if (seekCompleted)
        d->statusRegister[0] |= 0x20;
}

void FDC::generateFDCInterrupt(bool seekCompleted)
{
    updateStatus(seekCompleted);
    vlog(LogFDC, "Raise IRQ%s", seekCompleted ? " (seek completed)" : "");
    raiseIRQ();
}
