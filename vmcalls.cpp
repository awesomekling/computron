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

#include "CPU.h"
#include "Common.h"
#include "DiskDrive.h"
#include "debug.h"
#include "machine.h"
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#define FD_NO_ERROR 0x00
#define FD_BAD_COMMAND 0x01
#define FD_BAD_ADDRESS_MARK 0x02
#define FD_WRITE_PROTECT_ERROR 0x03
#define FD_SECTOR_NOT_FOUND 0x04
#define FD_FIXED_RESET_FAIL 0x05
#define FD_CHANGED_OR_REMOVED 0x06
#define FD_SEEK_FAIL 0x40
#define FD_TIMEOUT 0x80
#define FD_FIXED_NOT_READY 0xAA

enum DiskCallFunction { ReadSectors,
    WriteSectors,
    VerifySectors };
void bios_disk_call(CPU&, DiskCallFunction);
static void vm_handleE6(CPU& cpu);

void vm_call8(CPU& cpu, u16 port, u8 data)
{
    if (cpu.get_pe() && !cpu.get_vm())
        return;
    switch (port) {
    case 0xE0:
        vlog(LogAlert, "Interrupt %02X, function %04X requested", cpu.get_bl(), cpu.get_ax());
        if (cpu.get_bl() == 0x15 && cpu.get_ah() == 0x87) {
            vlog(LogAlert, "MoveBlock GDT{ %04X:%04X } x %04X", cpu.get_es(), cpu.get_si(), cpu.get_cx());
        }
        break;
    case 0xE6:
        vm_handleE6(cpu);
        break;
    case 0xE2:
        bios_disk_call(cpu, ReadSectors);
        break;
    case 0xE3:
        bios_disk_call(cpu, WriteSectors);
        break;
    case 0xE4:
        bios_disk_call(cpu, VerifySectors);
        break;
    default:
        vlog(LogAlert, "vm_call8: Unhandled write, %02X -> %04X", data, port);
        hard_exit(0);
        break;
    }
}

static DiskDrive* diskDriveForBIOSIndex(Machine& machine, u8 index)
{
    switch (index) {
    case 0x00:
        return &machine.floppy0();
        break;
    case 0x01:
        return &machine.floppy1();
        break;
    case 0x80:
        return &machine.fixed0();
        break;
    case 0x81:
        return &machine.fixed1();
        break;
    }
    return nullptr;
}

void vm_handleE6(CPU& cpu)
{
    extern u16 kbd_hit();
    extern u16 kbd_getc();

    struct tm* t;
    time_t curtime;
    struct timeval timv;
    u32 tick_count;
    DiskDrive* drive;

    switch (cpu.get_ax()) {
    case 0x1601:
        if (kbd_hit()) {
            cpu.set_ax(kbd_hit());
            cpu.set_zf(0);
        } else {
            cpu.set_ax(0);
            cpu.set_zf(1);
        }
        break;
    case 0x1A00:
        // Interrupt 1A, 00: Get RTC tick count
        cpu.set_al(0); // Midnight flag.
        curtime = time(nullptr);
        t = localtime(&curtime);
        tick_count = ((t->tm_hour * 3600) + (t->tm_min * 60) + (t->tm_sec)) * 18.206; // yuck..
        gettimeofday(&timv, NULL);
        tick_count += timv.tv_usec / 54926.9471602768;
#ifdef CT_DETERMINISTIC
        tick_count = 0x12345678;
#endif
        cpu.set_cx(most_significant<u16>(tick_count));
        cpu.set_dx(least_significant<u16>(tick_count));
        cpu.write_physical_memory<u32>(PhysicalAddress(0x046c), tick_count);
        break;
    case 0x1300:
        drive = diskDriveForBIOSIndex(cpu.machine(), cpu.get_dl());
        if (drive && drive->present()) {
            cpu.set_ah(FD_NO_ERROR);
            cpu.set_cf(0);
        } else {
            cpu.set_ah(FD_CHANGED_OR_REMOVED);
            cpu.set_cf(1);
        }
        cpu.write_physical_memory<u8>(PhysicalAddress(cpu.get_dl() < 2 ? 0x0441 : 0x0474), cpu.get_ah());
        break;
    case 0x1308:
        drive = diskDriveForBIOSIndex(cpu.machine(), cpu.get_dl());
        if (drive && drive->present()) {
            bool isFloppy = cpu.get_dl() < 2;
            u32 trax = drive->cylinders() - 1;
            cpu.set_al(0);
            cpu.set_ah(FD_NO_ERROR);
            cpu.set_bl(drive->floppyTypeForCMOS());
            cpu.set_bh(0);
            cpu.set_ch(trax & 0xFF);                                              // Tracks.
            cpu.set_cl(((trax >> 2) & 0xC0) | (drive->sectorsPerTrack() & 0x3F)); // Sectors per track.
            cpu.set_dh(drive->heads() - 1);                                       // Sides.

            if (isFloppy) {
                cpu.set_dl(cpu.machine().floppy0().present() + cpu.machine().floppy1().present());
            } else {
                cpu.set_dl(cpu.machine().fixed0().present() + cpu.machine().fixed1().present());
            }

            vlog(LogDisk, "Reporting %s geometry: %u tracks, %u spt, %u heads", qPrintable(drive->name()), drive->cylinders(), drive->sectorsPerTrack(), drive->heads());

            // FIXME: ES:DI should points to some wacky Disk Base Table.
            if (isFloppy) {
                cpu.set_es(0);
                cpu.set_di(0);
            }
            cpu.set_cf(0);
        } else {
            if (cpu.get_dl() < 2)
                cpu.set_ah(FD_CHANGED_OR_REMOVED);
            else if (cpu.get_dl() > 1)
                cpu.set_ah(FD_FIXED_NOT_READY);
            cpu.set_cf(1);
        }

        break;
    case 0x1315:
        drive = diskDriveForBIOSIndex(cpu.machine(), cpu.get_dl());
        if (drive && drive->present()) {
            cpu.set_ah(0x01); // Diskette, no change detection present.
            if (cpu.get_dl() > 1) {
                cpu.set_ah(0x03); // FIXED DISK :-)
                // If fixed disk, CX:DX = sectors.
                cpu.set_dx(drive->sectors());
                cpu.set_cx(drive->sectors() >> 16);
            }
            cpu.set_cf(0);
        } else {
            // Drive not present.
            cpu.set_ah(0);
            cpu.set_cf(1);
        }
        break;
    case 0x1318:
        drive = diskDriveForBIOSIndex(cpu.machine(), cpu.get_dl());
        if (drive && drive->present()) {
            vlog(LogDisk, "Setting media type for %s:", qPrintable(drive->name()));
            vlog(LogDisk, "%d sectors per track", cpu.get_cl());
            vlog(LogDisk, "%d tracks", cpu.get_ch());

            // FIXME: ES:DI should point to a Wacky DBT
            cpu.set_ah(0);
            cpu.set_cf(0);
        } else {
            cpu.set_cf(1);
            cpu.set_ah(0x80); // No media in drive
        }
        break;

    case 0x1600:
        cpu.set_ax(kbd_getc());
        break;

    case 0x1700:
        // Interrupt 17, 00: Print character on LPT
        {
            char tmp[80];
            sprintf(tmp, "prn%d.txt", cpu.get_dx());
            FILE* fpdrv = fopen(tmp, "a");
            fputc(cpu.get_cl(), fpdrv);
            fclose(fpdrv);
        }
        break;

    case 0x1A01:
        // Interrupt 1A, 01: Set RTC tick count
        vlog(LogAlert, "INT 1A,01: Attempt to set tick counter to %lu", (u32)(cpu.get_cx() << 16) | cpu.get_dx());
        break;

    case 0x1A05:
        // Interrupt 1A, 05: Set BIOS date
        vlog(LogAlert, "INT 1A,05: Attempt to set BIOS date to %02X-%02X-%04X", cpu.get_dh(), cpu.get_dl(), cpu.get_cx());
        break;

    /* 0x3333: Is Drive Present?
     * DL = Drive
     *
     * Returns:
     * AL = 0x01 if drive is present
     *    = 0x00 if not
     *
     * CF = !AL
     */
    case 0x3333:
        drive = diskDriveForBIOSIndex(cpu.machine(), cpu.get_dl());
        if (drive && drive->present()) {
            cpu.set_al(1);
            cpu.set_cf(0);
        } else {
            cpu.set_al(0);
            cpu.set_cf(1);
        }
        break;

    default:
        vlog(LogAlert, "Unknown VM call %04X received!!", cpu.get_ax());
        //hard_exit(0);
        break;
    }
}

static void bios_disk_read(CPU& cpu, FILE* fp, DiskDrive& drive, u16 cylinder, u16 head, u16 sector, u16 count, u16 segment, u16 offset)
{
    auto lba = drive.toLBA(cylinder, head, sector);

    if (options.disklog)
        vlog(LogDisk, "%s reading %u sectors at %u/%u/%u (LBA %u) to %04x:%04x", qPrintable(drive.name()), count, cylinder, head, sector, lba, segment, offset);

    QByteArray data(drive.bytesPerSector() * count, Qt::Uninitialized);
    fread(data.data(), drive.bytesPerSector(), count, fp);
    LinearAddress dest((segment << 4) + offset);
    for (int i = 0; i < data.size(); ++i)
        cpu.write_memory<u8>(dest.offset(i), data[i]);
}

static void bios_disk_write(CPU& cpu, FILE* fp, DiskDrive& drive, u16 cylinder, u16 head, u16 sector, u16 count, u16 segment, u16 offset)
{
    auto lba = drive.toLBA(cylinder, head, sector);

    if (options.disklog)
        vlog(LogDisk, "%s writing %u sectors at %u/%u/%u (LBA %u) from %04x:%04x", qPrintable(drive.name()), count, cylinder, head, sector, lba, segment, offset);

    const void* source = cpu.memory_pointer(LogicalAddress(segment, offset));
    fwrite(source, drive.bytesPerSector(), count, fp);
}

static void bios_disk_verify(CPU&, FILE* fp, DiskDrive& drive, u16 cylinder, u16 head, u16 sector, u16 count, u16 segment, u16 offset)
{
    auto lba = drive.toLBA(cylinder, head, sector);

    if (options.disklog)
        vlog(LogDisk, "%s verifying %u sectors at %u/%u/%u (LBA %u)", qPrintable(drive.name()), count, cylinder, head, sector, lba);

    u8 dummy[count * drive.bytesPerSector()];
    u16 veri = fread(dummy, drive.bytesPerSector(), count, fp);
    if (veri != count)
        vlog(LogAlert, "veri != count, something went wrong");

    // FIXME: Actually compare something..
    Q_UNUSED(segment);
    Q_UNUSED(offset);
}

void bios_disk_call(CPU& cpu, DiskCallFunction function)
{
    // This is a hack to support the custom Computron BIOS. We should not be here in (PE=1 && VM=0) mode.
    ASSERT(!cpu.get_pe() || cpu.get_vm());

    u16 cylinder = cpu.get_ch() | (((u16)cpu.get_cl() << 2) & 0x300);
    u16 sector = cpu.get_cl() & 0x3f;
    u8 driveIndex = cpu.get_dl();
    u8 head = cpu.get_dh();
    u16 sectorCount = cpu.get_al();
    FILE* fp;
    u32 lba;

    auto* drive = diskDriveForBIOSIndex(cpu.machine(), driveIndex);
    u8 error = FD_NO_ERROR;

    if (!drive || !drive->present()) {
        if (options.disklog)
            vlog(LogDisk, "Drive %02X not ready", driveIndex);
        if (!(driveIndex & 0x80))
            error = FD_CHANGED_OR_REMOVED;
        else
            error = FD_TIMEOUT;
        goto epilogue;
    }

    lba = drive->toLBA(cylinder, head, sector);
    if (lba > drive->sectors()) {
        if (options.disklog)
            vlog(LogDisk, "%s bogus sector request (LBA %u from CHS %u/%u/%u)", qPrintable(drive->name()), lba, cylinder, head, sector);
        error = FD_TIMEOUT;
        goto epilogue;
    }

    if ((sector > drive->sectorsPerTrack()) || (head >= drive->heads())) {
        if (options.disklog)
            vlog(LogDisk, "%s request out of geometrical bounds (%u/%u/%u)", qPrintable(drive->name()), cylinder, head, sector);
        error = FD_TIMEOUT;
        goto epilogue;
    }

    fp = fopen(qPrintable(drive->imagePath()), function == WriteSectors ? "rb+" : "rb");
    if (!fp) {
        vlog(LogDisk, "PANIC: Could not access drive %d image (%s)!", driveIndex, qPrintable(drive->imagePath()));
        hard_exit(1);
    }

    fseek(fp, lba * drive->bytesPerSector(), SEEK_SET);

    switch (function) {
    case ReadSectors:
        bios_disk_read(cpu, fp, *drive, cylinder, head, sector, sectorCount, cpu.get_es(), cpu.get_bx());
        break;
    case WriteSectors:
        bios_disk_write(cpu, fp, *drive, cylinder, head, sector, sectorCount, cpu.get_es(), cpu.get_bx());
        break;
    case VerifySectors:
        bios_disk_verify(cpu, fp, *drive, cylinder, head, sector, sectorCount, cpu.get_es(), cpu.get_bx());
        break;
    }

    error = FD_NO_ERROR;
    fclose(fp);

epilogue:
    if (error == FD_NO_ERROR) {
        cpu.set_cf(0);
        cpu.set_al(sectorCount);
    } else {
        cpu.set_cf(1);
        cpu.set_al(0);
    }

    cpu.set_ah(error);
    cpu.write_physical_memory<u8>(PhysicalAddress(0x441), error);
}
