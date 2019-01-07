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

#include "Common.h"
#include "CPU.h"
#include "pic.h"
#include "debug.h"
#include "machine.h"

//#define PIC_DEBUG

// FIXME: This should not be global.
std::atomic<WORD> PIC::s_pendingRequests;
static bool s_ignoringIRQs = false;

bool PIC::isIgnoringAllIRQs()
{
    return s_ignoringIRQs;
}

void PIC::setIgnoreAllIRQs(bool b)
{
    s_ignoringIRQs = b;
}

void PIC::updatePendingRequests(Machine& machine)
{
    WORD masterRequests = (machine.masterPIC().getIRR() & ~machine.masterPIC().getIMR());
    WORD slaveRequests = (machine.slavePIC().getIRR() & ~machine.slavePIC().getIMR());
    s_pendingRequests = masterRequests | (slaveRequests << 8);
#ifdef PIC_DEBUG
    if (machine.cpu().state() != CPU::Halted)
        vlog(LogPIC, "Pending requests: %04x", (WORD)s_pendingRequests);
#endif
}

PIC::PIC(bool isMaster, Machine& machine)
    : IODevice("PIC", machine)
    , m_baseAddress(isMaster ? 0x20 : 0xA0)
    , m_isrBase(isMaster ? 0x08 : 0x70)
    , m_irqBase(isMaster ? 0 : 8)
    , m_isMaster(isMaster)
{    
    listen(m_baseAddress, IODevice::ReadWrite);
    listen(m_baseAddress + 1, IODevice::ReadWrite);

    reset();
}

PIC::~PIC()
{
}

void PIC::reset()
{
    m_isr = 0x00;
    m_irr = 0x00;
    m_imr = 0xff;
    m_icw2Expected = false;
    m_icw4Expected = false;
    m_readISR = false;
    m_specialMaskMode = false;
    s_pendingRequests = 0;
}

void PIC::dumpMask()
{
    const char* green = "\033[32;1m";
    const char* red = "\033[31;1m";
    for (int i = 0; i < 8; ++i)
        vlog(LogPIC, " - IRQ %2u: %smask\033[0m %srequest\033[0m %sservice\033[0m",
                m_irqBase + i,
                (m_imr & (1 << i)) ? green : red,
                (m_irr & (1 << i)) ? green : red,
                (m_isr & (1 << i)) ? green : red);

}

void PIC::unmaskAll()
{
    m_imr = 0;
}

void PIC::writePort0(BYTE data)
{
    if (data & 0x10) {
#ifdef PIC_DEBUG
        vlog(LogPIC, "Got ICW1 %02X on port %02X", data, port);
        vlog(LogPIC, "[ICW1] ICW4 needed = %s", (data & 1) ? "yes" : "no");
        vlog(LogPIC, "[ICW1] Cascade = %s", (data & 2) ? "yes" : "no");
        vlog(LogPIC, "[ICW1] Vector size = %u", (data & 4) ? 4 : 8);
        vlog(LogPIC, "[ICW1] Level triggered = %s", (data & 8) ? "yes" : "no");
#endif
        m_imr = 0;
        m_isr = 0;
        m_irr = 0;
        m_readISR = false;
        m_specialMaskMode = false;
        m_icw2Expected = true;
        m_icw4Expected = data & 0x01;
        updatePendingRequests(machine());
        return;
    }

    if ((data & 0x18) == 0x08) {
#ifdef PIC_DEBUG
        vlog(LogPIC, "Got OCW3 %02X on port %02X", data, port);
#endif
        if (data & 0x02)
            m_readISR = data & 0x01;
        if (data & 0x04) {
            vlog(LogPIC, "PIC polling mode is not supported");
            ASSERT_NOT_REACHED();
        }
        if (data & 0x40)
            m_specialMaskMode = data & 0x20;
        return;
    }

    switch (data) {
    case 0x20: // non-specific EOI
        m_isr &= m_isr - 1;
        return;
    case 0x60:
    case 0x61:
    case 0x62:
    case 0x63:
    case 0x64:
    case 0x65:
    case 0x66:
    case 0x67: // specific EOI
        m_isr &= ~(1 << (data - 0x60));
        return;
    }

    if ((data & 0xc8) == 0xc0) {
        vlog(LogPIC, "Got that weird OCW2 thing that XENIX sends");
        return;
    }

    vlog(LogPIC, "Unhandled OCW2 %02X on port %02X", data, m_baseAddress + 0);
    ASSERT_NOT_REACHED();
}
void PIC::writePort1(BYTE data)
{
    if (((data & 0x07) == 0x00) && m_icw2Expected) {
#ifdef PIC_DEBUG
        vlog(LogPIC, "Got ICW2 %02X on port %02X", data, port);
#endif
        m_isrBase = data & 0xF8;
        m_icw2Expected = false;
        return;
    }

    // OCW1 - IMR write
#ifdef PIC_DEBUG
    vlog(LogPIC, "New IRQ mask set: %02X", data);
    for (int i = 0; i < 8; ++i)
        vlog(LogPIC, " - IRQ %u: %s", m_irqBase + i, (data & (1 << i)) ? "masked" : "service");
#endif
    m_imr = data;
    updatePendingRequests(machine());
}

void PIC::out8(WORD port, BYTE data)
{
    if (port & 1)
        return writePort1(data);
    writePort0(data);
}

BYTE PIC::in8(WORD port)
{
    if ((port & 1) == 0) {
        if (m_readISR) {
#ifdef PIC_DEBUG
            vlog(LogPIC, "Read ISR (%02x)", m_isr);
#endif
            return m_isr;
        }
#ifdef PIC_DEBUG
        vlog(LogPIC, "Read IRR (%02x)", m_irr);
#endif
        return m_irr;
    }
#ifdef PIC_DEBUG
    vlog(LogPIC, "Read IMR (%02x)", m_imr);
#endif
    return m_imr;
}

void PIC::raise(BYTE num)
{
    m_irr |= 1 << num;
}

void PIC::lower(BYTE num)
{
    m_irr &= 1 << num;
}

void PIC::raiseIRQ(Machine& machine, BYTE num)
{
    if (num < 8) {
        machine.masterPIC().raise(num);
    } else {
        machine.slavePIC().raise(num - 8);
        machine.masterPIC().raise(2);
    }

    updatePendingRequests(machine);
}

void PIC::lowerIRQ(Machine& machine, BYTE num)
{
    if (num < 8)
        machine.masterPIC().lower(num);
    else
        machine.slavePIC().lower(num - 8);

    updatePendingRequests(machine);
}

bool PIC::isIRQRaised(Machine& machine, BYTE num)
{
    if (num < 8)
        return machine.masterPIC().m_irr & (1 << num);
    else
        return machine.slavePIC().m_irr & (1 << (num - 8));

}

void PIC::serviceIRQ(CPU& cpu)
{
    if (s_ignoringIRQs)
        return;

    WORD pendingRequestsCopy = s_pendingRequests;
    if (!pendingRequestsCopy)
        return;

    Machine& machine = cpu.machine();

    BYTE irqToService = 0xFF;

    for (BYTE i = 0; i < 16; ++i) {
        if (i == 2)
            continue;
        if (pendingRequestsCopy & (1 << i)) {
            irqToService = i;
            break;
        }
    }

    if (irqToService == 0xFF)
        return;

    if (irqToService < 8) {
        machine.masterPIC().m_irr &= ~(1 << irqToService);
        machine.masterPIC().m_isr |= (1 << irqToService);

        cpu.interrupt(machine.masterPIC().m_isrBase | irqToService, CPU::InterruptSource::External);
    }
    else
    {
        machine.slavePIC().m_irr &= ~(1 << (irqToService - 8));
        machine.slavePIC().m_isr |= (1 << (irqToService - 8));

        machine.masterPIC().m_irr &= ~(1 << 2);
        machine.masterPIC().m_isr |= (1 << 2);

        cpu.interrupt(machine.slavePIC().m_isrBase | (irqToService - 8), CPU::InterruptSource::External);
    }

    updatePendingRequests(machine);

    cpu.setState(CPU::Alive);
}

PIC& PIC::master() const
{
    ASSERT(!m_isMaster);
    return machine().masterPIC();
}

PIC& PIC::slave() const
{
    ASSERT(m_isMaster);
    return machine().slavePIC();
}

