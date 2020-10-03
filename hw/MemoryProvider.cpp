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

#include "MemoryProvider.h"
#include "CPU.h"

const u8* MemoryProvider::memoryPointer(u32) const
{
    return nullptr;
}

void MemoryProvider::writeMemory8(u32, u8)
{
}

void MemoryProvider::writeMemory16(u32 address, u16 data)
{
    writeMemory8(address, leastSignificant<u8>(data));
    writeMemory8(address + 1, mostSignificant<u8>(data));
}

void MemoryProvider::writeMemory32(u32 address, u32 data)
{
    writeMemory16(address, leastSignificant<u16>(data));
    writeMemory16(address + 2, mostSignificant<u16>(data));
}

u8 MemoryProvider::readMemory8(u32)
{
    return 0;
}

u16 MemoryProvider::readMemory16(u32 address)
{
    return weld<u16>(readMemory8(address + 1), readMemory8(address));
}

u32 MemoryProvider::readMemory32(u32 address)
{
    return weld<u32>(readMemory16(address + 2), readMemory16(address));
}

void MemoryProvider::setSize(u32 size)
{
    RELEASE_ASSERT((size % 16384) == 0);
    m_size = size;
}
