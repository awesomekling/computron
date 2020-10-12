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

    PhysicalAddress base_address() const { return m_base_address; }
    u32 size() const { return m_size; }

    // pls no use :(
    virtual const u8* memory_pointer(u32 address) const;

    virtual u8 read_memory8(u32 address);
    virtual u16 read_memory16(u32 address);
    virtual u32 read_memory32(u32 address);
    virtual void write_memory8(u32 address, u8);
    virtual void write_memory16(u32 address, u16);
    virtual void write_memory32(u32 address, u32);

    const u8* pointer_for_direct_read_access() const { return m_pointer_for_direct_read_access; }

    template<typename T>
    T read(u32 address);
    template<typename T>
    void write(u32 address, T);

protected:
    MemoryProvider(PhysicalAddress base_address, u32 size = 0)
        : m_base_address(base_address)
    {
        set_size(size);
    }
    void set_size(u32);
    const u8* m_pointer_for_direct_read_access { nullptr };

private:
    PhysicalAddress m_base_address;
    u32 m_size { 0 };
};

template<typename T>
inline T MemoryProvider::read(u32 address)
{
    if (sizeof(T) == 1)
        return read_memory8(address);
    if (sizeof(T) == 2)
        return read_memory16(address);
    ASSERT(sizeof(T) == 4);
    return read_memory32(address);
}

template<typename T>
inline void MemoryProvider::write(u32 address, T data)
{
    if (sizeof(T) == 1)
        return write_memory8(address, data);
    if (sizeof(T) == 2)
        return write_memory16(address, data);
    ASSERT(sizeof(T) == 4);
    return write_memory32(address, data);
}
