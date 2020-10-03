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

#include "PS2.h"
#include "CPU.h"
#include "Common.h"
#include "machine.h"

//#define PS2_DEBUG

PS2::PS2(Machine& machine)
    : IODevice("PS2", machine)
{
    listen(0x92, IODevice::ReadWrite);
}

PS2::~PS2()
{
}

void PS2::reset()
{
    m_controlPortA = 0;
    machine().cpu().set_a20_enabled(false);
}

u8 PS2::in8(u16 port)
{
    if (port == 0x92) {
#ifdef PS2_DEBUG
        vlog(LogIO, "System Control Port A read, returning %02X", m_controlPortA);
#endif
        return m_controlPortA;
    }
    return IODevice::in8(port);
}

void PS2::out8(u16 port, u8 data)
{
    if (port == 0x92) {
#ifdef PS2_DEBUG
        vlog(LogIO, "A20=%u->%u (System Control Port A)", machine().cpu().isA20Enabled(), !!(data & 0x2));
#endif
        m_controlPortA = data;
        machine().cpu().set_a20_enabled(data & 0x2);
        return;
    }
    IODevice::out8(port, data);
}
