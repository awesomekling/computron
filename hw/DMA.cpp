// Computron x86 PC Emulator
// Copyright (C) 2003-2020 Andreas Kling <awesomekling@gmail.com>
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

#include "DMA.h"

struct DMA::Private {
};

DMA::DMA(Machine& machine)
    : IODevice("DMA", machine)
    , d(make<Private>())
{
    for (size_t i = 0; i <= 0xf; ++i)
        listen(i, IODevice::ReadWrite);
    for (size_t i = 0x80; i <= 0x8f; ++i)
        listen(i, IODevice::ReadWrite);
    for (size_t i = 0xc0; i <= 0xde; i += 2)
        listen(i, IODevice::ReadWrite);
}

DMA::~DMA()
{
}

void DMA::reset()
{
}

void DMA::out8(u16 port, u8 data)
{
    if (port == 0x80) {
        // Linux uses this port for small delays.
        return;
    }

    vlog(LogDMA, "out %04x <- %02x", port, data);
}

u8 DMA::in8(u16 port)
{
    vlog(LogDMA, "in %04x", port);
    return 0;
}
