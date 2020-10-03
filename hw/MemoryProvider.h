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

#pragma once

#include "debug.h"
#include "types.h"

class MemoryProvider {
public:
    virtual ~MemoryProvider() { }

    PhysicalAddress baseAddress() const { return m_baseAddress; }
    DWORD size() const { return m_size; }

    // pls no use :(
    virtual const BYTE* memoryPointer(DWORD address) const;

    virtual BYTE readMemory8(DWORD address);
    virtual WORD readMemory16(DWORD address);
    virtual DWORD readMemory32(DWORD address);
    virtual void writeMemory8(DWORD address, BYTE);
    virtual void writeMemory16(DWORD address, WORD);
    virtual void writeMemory32(DWORD address, DWORD);

    const BYTE* pointerForDirectReadAccess() const { return m_pointerForDirectReadAccess; }

    template<typename T>
    T read(DWORD address);
    template<typename T>
    void write(DWORD address, T);

protected:
    MemoryProvider(PhysicalAddress baseAddress, DWORD size = 0)
        : m_baseAddress(baseAddress)
    {
        setSize(size);
    }
    void setSize(DWORD);
    const BYTE* m_pointerForDirectReadAccess { nullptr };

private:
    PhysicalAddress m_baseAddress;
    DWORD m_size { 0 };
};

template<typename T>
inline T MemoryProvider::read(DWORD address)
{
    if (sizeof(T) == 1)
        return readMemory8(address);
    if (sizeof(T) == 2)
        return readMemory16(address);
    ASSERT(sizeof(T) == 4);
    return readMemory32(address);
}

template<typename T>
inline void MemoryProvider::write(DWORD address, T data)
{
    if (sizeof(T) == 1)
        return writeMemory8(address, data);
    if (sizeof(T) == 2)
        return writeMemory16(address, data);
    ASSERT(sizeof(T) == 4);
    return writeMemory32(address, data);
}
