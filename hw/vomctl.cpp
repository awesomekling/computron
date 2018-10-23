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

#include "vomctl.h"
#include "CPU.h"
#include "Common.h"
#include "debug.h"
#include "machine.h"
#include <stdio.h>

struct VomCtl::Private
{
    QString consoleWriteBuffer;
};

VomCtl::VomCtl(Machine& machine)
    : IODevice("VomCtl", machine)
    , d(make<Private>())
{
    listen(0xD6, IODevice::ReadWrite);
    listen(0xD7, IODevice::ReadWrite);
    listen(0xE9, IODevice::WriteOnly);

    // FIXME: These should all be removed.
    listen(0xE0, IODevice::WriteOnly);
    listen(0xE2, IODevice::WriteOnly);
    listen(0xE3, IODevice::WriteOnly);
    listen(0xE4, IODevice::WriteOnly);
    listen(0xE6, IODevice::WriteOnly);
    listen(0xE7, IODevice::WriteOnly);
    listen(0xE8, IODevice::WriteOnly);

    listen(0x666, IODevice::WriteOnly);

    reset();
}

VomCtl::~VomCtl()
{
}

void VomCtl::reset()
{
    m_registerIndex = 0;
    d->consoleWriteBuffer = QString();
}

BYTE VomCtl::in8(WORD port)
{
    switch (port) {
    case 0xD6: // VOMCTL_REGISTER
        vlog(LogVomCtl, "Read register %02X", m_registerIndex);
        switch (m_registerIndex) {
        case 0x00: // Always 0
            return 0;
        case 0x01: // Get CPU type
            return 3;
        case 0x02: // RAM size LSB
            return leastSignificant<BYTE>(machine().cpu().baseMemorySize() / 1024);
        case 0x03: // RAM size MSB
            return mostSignificant<BYTE>(machine().cpu().baseMemorySize() / 1024);
        }
        vlog(LogVomCtl, "Invalid register %02X read", m_registerIndex);
        return IODevice::JunkValue;
    case 0xD7: // VOMCTL_CONSOLE_WRITE
        vlog(LogVomCtl, "%s", d->consoleWriteBuffer.toLatin1().constData());
        d->consoleWriteBuffer.clear();
        return IODevice::JunkValue;
    default:
        return IODevice::in8(port);
    }
}

void VomCtl::out8(WORD port, BYTE data)
{
    extern void vm_call8(CPU&, WORD port, BYTE value);

    switch (port) {
    case 0xD6: // VOMCTL_REGISTER
        //vlog(LogVomCtl, "Select register %02X", data);
        m_registerIndex = data;
        break;
    case 0xD7: // VOMCTL_CONSOLE_WRITE
        d->consoleWriteBuffer += QChar::fromLatin1(data);
        break;
    case 0xE0:
    case 0xE2:
    case 0xE3:
    case 0xE4:
    case 0xE6:
    case 0xE7:
    case 0xE8:
        vm_call8(machine().cpu(), port, data);
        break;
    case 0xE9:
    case 0x666:
        {
            static FILE* fp = fopen("out.txt", "w");
            fputc(data, fp);
            fflush(fp);
        }
        break;
    default:
        IODevice::out8(port, data);
    }
}
