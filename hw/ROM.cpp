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
#include "Common.h"
#include <QFile>

ROM::ROM(PhysicalAddress baseAddress, const QString& fileName)
    : MemoryProvider(baseAddress)
{
    QFile file(fileName);
    vlog(LogConfig, "Build ROM for %08x with file %s", baseAddress, qPrintable(fileName));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    m_data = file.readAll();
    setSize(m_data.size());
    m_pointerForDirectReadAccess = reinterpret_cast<const BYTE*>(m_data.data());
}

ROM::~ROM()
{
}

bool ROM::isValid() const
{
    return !m_data.isNull();
}

BYTE ROM::readMemory8(DWORD address)
{
    return m_data.data()[address - baseAddress().get()];
}

void ROM::writeMemory8(DWORD address, BYTE data)
{
    vlog(LogAlert, "Write to ROM address %08x, data %02x", address, data);
}

const BYTE* ROM::memoryPointer(DWORD address)
{
    return reinterpret_cast<const BYTE*>(&m_data.data()[address - baseAddress().get()]);
}
