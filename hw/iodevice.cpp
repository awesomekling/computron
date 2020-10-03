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

#include "iodevice.h"
#include "debug.h"
#include "machine.h"
#include "pic.h"
#include <QList>

//#define IODEVICE_DEBUG
//#define IRQ_DEBUG

QSet<u16> IODevice::s_ignorePorts;

IODevice::IODevice(const char* name, Machine& machine, int irq)
    : m_machine(machine)
    , m_name(name)
    , m_irq(irq)
{
    m_machine.registerDevice(Badge<IODevice>(), *this);
}

IODevice::~IODevice()
{
    m_machine.unregisterDevice(Badge<IODevice>(), *this);
}

void IODevice::listen(u16 port, ListenMask mask)
{
    if (mask & ReadOnly)
        machine().registerInputDevice(Badge<IODevice>(), port, *this);

    if (mask & WriteOnly)
        machine().registerOutputDevice(Badge<IODevice>(), port, *this);

    m_ports.append(port);
}

QList<u16> IODevice::ports() const
{
    return m_ports;
}

const char* IODevice::name() const
{
    return m_name;
}

void IODevice::out8(u16 port, u8 data)
{
    vlog(LogIO, "FIXME: IODevice[%s]::out8(%04X, %02X)", m_name, port, data);
}

void IODevice::out16(u16 port, u16 data)
{
#ifdef IODEVICE_DEBUG
    vlog(LogIO, "IODevice[%s]::out16(%04x) fallback to multiple out8() calls", m_name, port);
#endif
    out8(port, leastSignificant<u8>(data));
    out8(port + 1, mostSignificant<u8>(data));
}

void IODevice::out32(u16 port, u32 data)
{
#ifdef IODEVICE_DEBUG
    vlog(LogIO, "IODevice[%s]::out32(%04x) fallback to multiple out8() calls", m_name, port);
#endif
    out8(port + 0, leastSignificant<u8>(leastSignificant<u16>(data)));
    out8(port + 1, mostSignificant<u8>(leastSignificant<u16>(data)));
    out8(port + 2, leastSignificant<u8>(mostSignificant<u16>(data)));
    out8(port + 3, mostSignificant<u8>(mostSignificant<u16>(data)));
}

u8 IODevice::in8(u16 port)
{
    vlog(LogIO, "FIXME: IODevice[%s]::in8(%04X)", m_name, port);
    return IODevice::JunkValue;
}

u16 IODevice::in16(u16 port)
{
#ifdef IODEVICE_DEBUG
    vlog(LogIO, "IODevice[%s]::in16(%04x) fallback to multiple in8() calls", m_name, port);
#endif
    return weld<u16>(in8(port + 1), in8(port));
}

u32 IODevice::in32(u16 port)
{
#ifdef IODEVICE_DEBUG
    vlog(LogIO, "IODevice[%s]::in32(%04x) fallback to multiple in8() calls", m_name, port);
#endif
    return weld<u32>(in16(port + 2), in16(port));
}

void IODevice::ignorePort(u16 port)
{
    s_ignorePorts.insert(port);
}

bool IODevice::shouldIgnorePort(u16 port)
{
    return s_ignorePorts.contains(port);
}

void IODevice::raiseIRQ()
{
    ASSERT(m_irq != -1);
    ASSERT(m_irq < 256);
#ifdef IRQ_DEBUG
    if (!isIRQRaised())
        vlog(LogPIC, "\033[35;1mRaise IRQ %d\033[0m", m_irq);
#endif
    PIC::raiseIRQ(machine(), m_irq);
}

void IODevice::lowerIRQ()
{
    ASSERT(m_irq != -1);
    ASSERT(m_irq < 256);
#ifdef IRQ_DEBUG
    if (isIRQRaised())
        vlog(LogPIC, "Lower IRQ %d", m_irq);
#endif
    PIC::lowerIRQ(machine(), m_irq);
}

bool IODevice::isIRQRaised() const
{
    ASSERT(m_irq != -1);
    ASSERT(m_irq < 256);
    return PIC::isIRQRaised(machine(), m_irq);
}
