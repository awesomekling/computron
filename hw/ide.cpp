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
    DiskDrive& drive() { return *drivePtr; }

    unsigned controllerIndex { 0xffffffff };
    DiskDrive* drivePtr { nullptr };

    u16 cylinderIndex { 0 };
    u8 sectorIndex { 0 };
    u8 headIndex { 0 };
    u8 sectorCount { 0 };
    u8 error { 0 };
    bool inLBAMode { false };

    void identify(IDE&);
    void readSectors(IDE&);
    void writeSectors();

    u32 lba()
    {
        if (inLBAMode) {
            return ((u32)cylinderIndex << 8) | sectorIndex;
        }
        return drive().toLBA(cylinderIndex, headIndex, sectorIndex);
    }

    template<typename T>
    T readFromSectorBuffer();
    template<typename T>
    void writeToSectorBuffer(IDE&, T);

    QByteArray m_readBuffer;
    int m_readBufferIndex { 0 };

    QByteArray m_writeBuffer;
    int m_writeBufferIndex { 0 };
};

void IDEController::identify(IDE& ide)
{
    u16 data[256];
    memset(data, 0, sizeof(data));
    data[1] = drive().sectors() / (drive().sectorsPerTrack() * drive().heads());
    data[3] = drive().heads();
    data[6] = drive().sectorsPerTrack();
    m_readBuffer.resize(512);
    memcpy(m_readBuffer.data(), data, sizeof(data));
    strcpy(m_readBuffer.data() + 54, "oCpmtuor niDks");
    m_readBufferIndex = 0;
    ide.raiseIRQ();
}

void IDEController::readSectors(IDE& ide)
{
#ifdef IDE_DEBUG
    vlog(LogIDE, "ide%u: Read sectors (LBA: %u, count: %u)", controllerIndex, lba(), sectorCount);
#endif
    FILE* f = fopen(qPrintable(drive().imagePath()), "rb");
    RELEASE_ASSERT(f);
    m_readBuffer.resize(drive().bytesPerSector() * sectorCount);
    int result;
    result = fseek(f, lba() * drive().bytesPerSector(), SEEK_SET);
    ASSERT(result != -1);
    result = fread(m_readBuffer.data(), drive().bytesPerSector(), sectorCount, f);
    ASSERT(result != -1);
    result = fclose(f);
    ASSERT(result != -1);
    m_readBufferIndex = 0;
    ide.raiseIRQ();
}

void IDEController::writeSectors()
{
    vlog(LogIDE, "ide%u: Write sectors (LBA: %u, count: %u)", controllerIndex, lba(), sectorCount);
    m_writeBuffer.resize(drive().bytesPerSector() * sectorCount);
    m_writeBufferIndex = 0;
}

template<typename T>
void IDEController::writeToSectorBuffer(IDE& ide, T data)
{
    if (m_writeBufferIndex >= m_writeBuffer.size()) {
        vlog(LogIDE, "ide%u: Write buffer already full!");
        return;
    }
    if ((m_writeBufferIndex + static_cast<int>(sizeof(T))) > m_writeBuffer.size()) {
        vlog(LogIDE, "ide%u: Not enough space left in write buffer!");
        ASSERT_NOT_REACHED();
        return;
    }
    T* bufferPtr = reinterpret_cast<T*>(&m_writeBuffer.data()[m_writeBufferIndex]);
    *bufferPtr = data;
    m_writeBufferIndex += sizeof(T);
    if (m_writeBufferIndex < m_writeBuffer.size())
        return;
    vlog(LogIDE, "ide%u: Got all sector data, flushing to disk!", controllerIndex);
    FILE* f = fopen(qPrintable(drive().imagePath()), "rb+");
    RELEASE_ASSERT(f);
    int result;
    result = fseek(f, lba() * drive().bytesPerSector(), SEEK_SET);
    ASSERT(result != -1);
    result = fwrite(m_writeBuffer.data(), drive().bytesPerSector(), sectorCount, f);
    ASSERT(result != -1);
    result = fclose(f);
    ASSERT(result != -1);
    ide.raiseIRQ();
}

template<typename T>
T IDEController::readFromSectorBuffer()
{
    if (m_readBufferIndex >= m_readBuffer.size()) {
        vlog(LogIDE, "ide%u: No data left in read buffer!", controllerIndex);
        return 0;
    }
    if ((m_readBufferIndex + static_cast<int>(sizeof(T))) > m_readBuffer.size()) {
        vlog(LogIDE, "ide%u: Not enough data left in read buffer!", controllerIndex);
        ASSERT_NOT_REACHED();
        return 0;
    }
    const T* data = reinterpret_cast<T*>(&m_readBuffer.data()[m_readBufferIndex]);
    m_readBufferIndex += sizeof(T);
    return *data;
}

static const int gNumControllers = 2;

struct IDE::Private {
    IDEController controller[gNumControllers];
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
    d->controller[0].controllerIndex = 0;
    d->controller[0].drivePtr = &machine().fixed0();
    d->controller[1] = IDEController();
    d->controller[1].controllerIndex = 1;
    d->controller[1].drivePtr = &machine().fixed1();
}

void IDE::out8(u16 port, u8 data)
{
#ifdef IDE_DEBUG
    vlog(LogIDE, "out8 %03x, %02x", port, data);
#endif

    const int controllerIndex = (((port)&0x1F0) == 0x170);
    IDEController& controller = d->controller[controllerIndex];

    switch (port & 0xF) {
    case 0x0:
        controller.writeToSectorBuffer<u8>(*this, data);
        break;
    case 0x2:
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d sector count set: %u", controllerIndex, data);
#endif
        controller.sectorCount = data;
        break;
    case 0x3:
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d sector index set: %u", controllerIndex, data);
#endif
        controller.sectorIndex = data;
        break;
    case 0x4:
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d cylinder LSB set: %u", controllerIndex, data);
#endif
        controller.cylinderIndex = weld<u16>(mostSignificant<u8>(controller.cylinderIndex), data);
        break;
    case 0x5:
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d cylinder MSB set: %u", controllerIndex, data);
#endif
        controller.cylinderIndex = weld<u16>(data, leastSignificant<u8>(controller.cylinderIndex));
        break;
    case 0x6:
        controller.headIndex = data & 0xf;
        controller.inLBAMode = data & 0x40;
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d head index set: %u", controllerIndex, controller.headIndex);
        vlog(LogIDE, "Controller %d in %s mode", controllerIndex, controller.inLBAMode ? "LBA" : "CHS");
#endif
        break;
    case 0x7:
        // FIXME: ...
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d received command %02X", controllerIndex, data);
#endif
        executeCommand(controller, data);
        break;
    default:
        IODevice::out8(port, data);
    }
}

u8 IDE::in8(u16 port)
{
    int controllerIndex = (((port)&0x1F0) == 0x170);
    IDEController& controller = d->controller[controllerIndex];

    // FIXME: This port should maybe be managed by the FDC?
    if (port == 0x3f6) {
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d alternate status queried: %02X", controllerIndex, status(controller));
#endif
        return status(controller);
    }

    switch (port & 0xF) {
    case 0:
        return controller.readFromSectorBuffer<u8>();
    case 0x1:
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d error queried: %02X", controllerIndex, controller.error);
#endif
        return controller.error;
    case 0x2:
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d sector count queried: %u", controllerIndex, controller.sectorCount);
#endif
        return controller.sectorCount;
    case 0x3:
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d sector index queried: %u", controllerIndex, controller.sectorIndex);
#endif
        return controller.sectorIndex;
    case 0x4:
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d cylinder LSB queried: %02X", controllerIndex, leastSignificant<BYTE>(controller.cylinderIndex));
#endif
        return leastSignificant<u8>(controller.cylinderIndex);
    case 0x5:
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d cylinder MSB queried: %02X", controllerIndex, mostSignificant<BYTE>(controller.cylinderIndex));
#endif
        return mostSignificant<u8>(controller.cylinderIndex);
    case 0x6:
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d head index queried: %u", controllerIndex, controller.headIndex);
#endif
        return controller.headIndex;
    case 0x7: {
        u8 ret = status(controller);
#ifdef IDE_DEBUG
        vlog(LogIDE, "Controller %d status queried: %02X", controllerIndex, ret);
#endif
        return ret;
    }
    default:
        return IODevice::in8(port);
    }
}

u16 IDE::in16(u16 port)
{
    int controllerIndex = (((port)&0x1f0) == 0x170);
    IDEController& controller = d->controller[controllerIndex];

    switch (port & 0xF) {
    case 0:
        return controller.readFromSectorBuffer<u16>();
    default:
        return IODevice::in16(port);
    }
}

u32 IDE::in32(u16 port)
{
    int controllerIndex = (((port)&0x1f0) == 0x170);
    IDEController& controller = d->controller[controllerIndex];

    switch (port & 0xF) {
    case 0:
        return controller.readFromSectorBuffer<u32>();
    default:
        return IODevice::in16(port);
    }
}

void IDE::out16(u16 port, u16 data)
{
#ifdef IDE_DEBUG
    vlog(LogIDE, "out16 %03x, %04x", port, data);
#endif

    const int controllerIndex = (((port)&0x1F0) == 0x170);
    IDEController& controller = d->controller[controllerIndex];

    switch (port & 0xF) {
    case 0x0:
        controller.writeToSectorBuffer<u16>(*this, data);
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

    const int controllerIndex = (((port)&0x1F0) == 0x170);
    IDEController& controller = d->controller[controllerIndex];

    switch (port & 0xF) {
    case 0x0:
        controller.writeToSectorBuffer<u32>(*this, data);
        break;
    default:
        return IODevice::out16(port, data);
    }
}

void IDE::executeCommand(IDEController& controller, u8 command)
{
    switch (command) {
    case 0x20:
    case 0x21:
        controller.readSectors(*this);
        break;
    case 0x30:
        controller.writeSectors();
        break;
    case 0xEC:
        controller.identify(*this);
        break;
#if 0
    case 0x90:
        // Run diagnostics, FIXME: this isn't a very nice implementation lol.
        controller.error = 0;
        raiseIRQ();
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
    if (controller.m_readBufferIndex < controller.m_readBuffer.size()) {
        status |= DRQ;
    }
    if (controller.m_writeBufferIndex < controller.m_writeBuffer.size()) {
        status |= DRQ;
    }

    return static_cast<Status>(status);
}
