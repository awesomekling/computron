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

#include "ROM.h"
#include "CPU.h"
#include "Common.h"
#include "debugger.h"
#include <QFile>

ROM::ROM(PhysicalAddress base_address, const QString& file_name)
    : MemoryProvider(base_address)
{
    QFile file(file_name);
    vlog(LogConfig, "Build ROM for %08x with file %s", base_address, qPrintable(file_name));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    m_data = file.readAll();
    set_size(m_data.size());
    m_pointer_for_direct_read_access = reinterpret_cast<const u8*>(m_data.data());
}

ROM::~ROM()
{
}

bool ROM::is_valid() const
{
    return !m_data.isNull();
}

u8 ROM::read_memory8(u32 address)
{
    return m_data.data()[address - base_address().get()];
}

void ROM::write_memory8(u32 address, u8 data)
{
    vlog(LogAlert, "Write to ROM address %08x, data %02x", address, data);
#ifdef DEBUG_SERENITY
    if (options.serenity)
        g_cpu->debugger().enter();
#endif
}

const u8* ROM::memory_pointer(u32 address) const
{
    return reinterpret_cast<const u8*>(&m_data.data()[address - base_address().get()]);
}
