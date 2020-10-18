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

#include "ide.h"
#include "Common.h"
#include "DiskDrive.h"
#include "debug.h"
#include "machine.h"

//#define IDE_DEBUG

struct IDEController {
    DiskDrive& drive() { return *drive_ptr; }

    unsigned controller_index { 0xffffffff };
    DiskDrive* drive_ptr { nullptr };

    u16 cylinder_index { 0 };
    u8 sector_index { 0 };
    u8 head_index { 0 };
    u8 sector_count { 0 };
    u8 error { 0 };
    bool in_lba_mode { false };

    void identify(IDE&);
    void read_sectors(IDE&);
    void write_sectors();

    u32 lba()
    {
        if (in_lba_mode) {
            return ((u32)cylinder_index << 8) | sector_index;
        }
        return drive().to_lba(cylinder_index, head_index, sector_index);
    }

    template<typename T>
    T read_from_sector_buffer();
    template<typename T>
    void write_to_sector_buffer(IDE&, T);

    QByteArray m_read_buffer;
    int m_read_buffer_index { 0 };

    QByteArray m_write_buffer;
    int m_write_buffer_index { 0 };
};

void IDEController::identify(IDE& ide)
{
    u16 data[256];
    memset(data, 0, sizeof(data));
    data[1] = drive().sectors() / (drive().sectors_per_track() * drive().heads());
    data[3] = drive().heads();
    data[6] = drive().sectors_per_track();
    m_read_buffer.resize(512);
    memcpy(m_read_buffer.data(), data, sizeof(data));
    strcpy(m_read_buffer.data() + 54, "oCpmtuor niDks");
    m_read_buffer_index = 0;
    ide.raise_irq();
}

void IDEController::read_sectors(IDE& ide)
{
#ifdef IDE_DEBUG
    vlog(LogIDE, "ide%u: Read sectors (LBA: %u, count: %u)", controller_index, lba(), sector_count);
#endif
    FILE* f = fopen(qPrintable(drive().image_path()), "rb");
    RELEASE_ASSERT(f);
    m_read_buffer.resize(drive().bytes_per_sector() * sector_count);
    int result;
    result = fseek(f, lba() * drive().bytes_per_sector(), SEEK_SET);
    ASSERT(result != -1);
    result = fread(m_read_buffer.data(), drive().bytes_per_sector(), sector_count, f);
    ASSERT(result != -1);
    result = fclose(f);
    ASSERT(result != -1);
    m_read_buffer_index = 0;
    ide.raise_irq();
}

void IDEController::write_sectors()
{
    vlog(LogIDE, "ide%u: Write sectors (LBA: %u, count: %u)", controller_index, lba(), sector_count);
    m_write_buffer.resize(drive().bytes_per_sector() * sector_count);
    m_write_buffer_index = 0;
}

template<typename T>
void IDEController::write_to_sector_buffer(IDE& ide, T data)
{
    if (m_write_buffer_index >= m_write_buffer.size()) {
        vlog(LogIDE, "ide%u: Write buffer already full!");
        return;
    }
    if ((m_write_buffer_index + static_cast<int>(sizeof(T))) > m_write_buffer.size()) {
        vlog(LogIDE, "ide%u: Not enough space left in write buffer!");
        ASSERT_NOT_REACHED();
        return;
    }
    T* buffer_ptr = reinterpret_cast<T*>(&m_write_buffer.data()[m_write_buffer_index]);
    *buffer_ptr = data;
    m_write_buffer_index += sizeof(T);
    if (m_write_buffer_index < m_write_buffer.size())
        return;
    vlog(LogIDE, "ide%u: Got all sector data, flushing to disk!", controller_index);
    FILE* f = fopen(qPrintable(drive().image_path()), "rb+");
    RELEASE_ASSERT(f);
    int result;
    result = fseek(f, lba() * drive().bytes_per_sector(), SEEK_SET);
    ASSERT(result != -1);
    result = fwrite(m_write_buffer.data(), drive().bytes_per_sector(), sector_count, f);
    ASSERT(result != -1);
    result = fclose(f);
    ASSERT(result != -1);
    ide.raise_irq();
}

template<typename T>
T IDEController::read_from_sector_buffer()
{
    if (m_read_buffer_index >= m_read_buffer.size()) {
        vlog(LogIDE, "ide%u: No data left in read buffer!", controller_index);
        return 0;
    }
    if ((m_read_buffer_index + static_cast<int>(sizeof(T))) > m_read_buffer.size()) {
        vlog(LogIDE, "ide%u: Not enough data left in read buffer!", controller_index);
        ASSERT_NOT_REACHED();
        return 0;
    }
    const T* data = reinterpret_cast<T*>(&m_read_buffer.data()[m_read_buffer_index]);
    m_read_buffer_index += sizeof(T);
    return *data;
}

static const int num_controllers = 2;

struct IDE::Private {
    IDEController controller[num_controllers];
};

IDE::IDE(Machine& machine)
    : IODevice("IDE", machine, 14)
    , d(make<Private>())
{
    listen(0x170, IODevice::ReadWrite);
    listen(0x171, IODevice::ReadOnly);
    listen(0x172, IODevice::ReadWrite);
    listen(0x173, IODevice::ReadWrite);
    listen(0x174, IODevice::ReadWrite);
    listen(0x175, IODevice::ReadWrite);
    listen(0x176, IODevice::ReadWrite);
    listen(0x177, IODevice::ReadWrite);
    listen(0x1F0, IODevice::ReadWrite);
    listen(0x1F1, IODevice::ReadOnly);
    listen(0x1F2, IODevice::ReadWrite);
    listen(0x1F3, IODevice::ReadWrite);
    listen(0x1F4, IODevice::ReadWrite);
    listen(0x1F5, IODevice::ReadWrite);
    listen(0x1F6, IODevice::ReadWrite);
    listen(0x1F7, IODevice::ReadWrite);

    listen(0x3f6, IODevice::ReadOnly);

    reset();
}

IDE::~IDE()
{
}

void IDE::reset()
{
    d->controller[0] = IDEController();
    d->controller[0].controller_index = 0;
    d->controller[0].drive_ptr = &machine().fixed0();
    d->controller[1] = IDEController();
    d->controller[1].controller_index = 1;
    d->controller[1].drive_ptr = &machine().fixed1();
}

void IDE::out8(u16 port, u8 data)
{
#ifdef IDE_DEBUG
    vlog(LogIDE, "out8 %03x, %02x", port, data);
#endif

    const int controller_index = (((port)&0x1F0) == 0x170);
    IDEController& controller = d->controller[controller_index];

    switch (port & 0xF) {
    case 0x0:
        controller.write_to_sector_buffer<u8>(*this, data);
        break;
    case 0x2:
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d sector count set: %u", controller_index, data);
#endif
        controller.sector_count = data;
        break;
    case 0x3:
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d sector index set: %u", controller_index, data);
#endif
        controller.sector_index = data;
        break;
    case 0x4:
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d cylinder LSB set: %u", controller_index, data);
#endif
        controller.cylinder_index = weld<u16>(most_significant<u8>(controller.cylinder_index), data);
        break;
    case 0x5:
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d cylinder MSB set: %u", controller_index, data);
#endif
        controller.cylinder_index = weld<u16>(data, least_significant<u8>(controller.cylinder_index));
        break;
    case 0x6:
        controller.head_index = data & 0xf;
        controller.in_lba_mode = data & 0x40;
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d head index set: %u", controller_index, controller.head_index);
        vlog(LogIDE, "Controller %d in %s mode", controller_index, controller.inLBAMode ? "LBA" : "CHS");
#endif
        break;
    case 0x7:
        // FIXME: ...
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d received command %02X", controller_index, data);
#endif
        execute_command(controller, data);
        break;
    default:
        IODevice::out8(port, data);
    }
}

u8 IDE::in8(u16 port)
{
    int controller_index = (((port)&0x1F0) == 0x170);
    IDEController& controller = d->controller[controller_index];

    // FIXME: This port should maybe be managed by the FDC?
    if (port == 0x3f6) {
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d alternate status queried: %02X", controller_index, status(controller));
#endif
        return status(controller);
    }

    switch (port & 0xF) {
    case 0:
        return controller.read_from_sector_buffer<u8>();
    case 0x1:
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d error queried: %02X", controller_index, controller.error);
#endif
        return controller.error;
    case 0x2:
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d sector count queried: %u", controller_index, controller.sector_count);
#endif
        return controller.sector_count;
    case 0x3:
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d sector index queried: %u", controller_index, controller.sectorIndex);
#endif
        return controller.sector_index;
    case 0x4:
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d cylinder LSB queried: %02X", controller_index, least_significant<BYTE>(controller.cylinderIndex));
#endif
        return least_significant<u8>(controller.cylinder_index);
    case 0x5:
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d cylinder MSB queried: %02X", controller_index, most_significant<BYTE>(controller.cylinderIndex));
#endif
        return most_significant<u8>(controller.cylinder_index);
    case 0x6:
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d head index queried: %u", controller_index, controller.head_index);
#endif
        return controller.head_index;
    case 0x7: {
        u8 ret = status(controller);
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d status queried: %02X", controller_index, ret);
#endif
        return ret;
    }
    default:
        return IODevice::in8(port);
    }
}

u16 IDE::in16(u16 port)
{
    int controller_index = (((port)&0x1f0) == 0x170);
    IDEController& controller = d->controller[controller_index];

    switch (port & 0xF) {
    case 0:
        return controller.read_from_sector_buffer<u16>();
    default:
        return IODevice::in16(port);
    }
}

u32 IDE::in32(u16 port)
{
    int controller_index = (((port)&0x1f0) == 0x170);
    IDEController& controller = d->controller[controller_index];

    switch (port & 0xF) {
    case 0:
        return controller.read_from_sector_buffer<u32>();
    default:
        return IODevice::in16(port);
    }
}

void IDE::out16(u16 port, u16 data)
{
#ifdef IDE_DEBUG
    vlog(LogIDE, "out16 %03x, %04x", port, data);
#endif

    const int controller_index = (((port)&0x1F0) == 0x170);
    IDEController& controller = d->controller[controller_index];

    switch (port & 0xF) {
    case 0x0:
        controller.write_to_sector_buffer<u16>(*this, data);
        break;
    default:
        return IODevice::out16(port, data);
    }
}

void IDE::out32(u16 port, u32 data)
{
#ifdef IDE_DEBUG
    vlog(LogIDE, "out32 %03x, %08x", port, data);
#endif

    const int controller_index = (((port)&0x1F0) == 0x170);
    IDEController& controller = d->controller[controller_index];

    switch (port & 0xF) {
    case 0x0:
        controller.write_to_sector_buffer<u32>(*this, data);
        break;
    default:
        return IODevice::out16(port, data);
    }
}

void IDE::execute_command(IDEController& controller, u8 command)
{
    switch (command) {
    case 0x20:
    case 0x21:
        controller.read_sectors(*this);
        break;
    case 0x30:
        controller.write_sectors();
        break;
    case 0xEC:
        controller.identify(*this);
        break;
#if 0
    case 0x90:
        // Run diagnostics, FIXME: this isn't a very nice implementation lol.
        controller.error = 0;
        raise_irq();
        break;
#endif
    default:
        vlog(LogIDE, "Unknown command %02x", command);
        break;
    }
}

IDE::Status IDE::status(const IDEController& controller) const
{
    // FIXME: ...
    unsigned status = INDEX | DRDY;
    if (controller.m_read_buffer_index < controller.m_read_buffer.size()) {
        status |= DRQ;
    }
    if (controller.m_write_buffer_index < controller.m_write_buffer.size()) {
        status |= DRQ;
    }

    return static_cast<Status>(status);
}
