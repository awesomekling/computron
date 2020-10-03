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
#include <QList>

class Machine;

class IODevice {
public:
    IODevice(const char* name, Machine&, int irq = -1);
    virtual ~IODevice();

    const char* name() const;
    Machine& machine() const { return m_machine; }

    void raiseIRQ();
    void lowerIRQ();
    bool isIRQRaised() const;

    virtual void reset() = 0;

    template<typename T>
    T in(WORD port);
    template<typename T>
    void out(WORD port, T data);

    virtual BYTE in8(WORD port);
    virtual WORD in16(WORD port);
    virtual DWORD in32(WORD port);
    virtual void out8(WORD port, BYTE data);
    virtual void out16(WORD port, WORD data);
    virtual void out32(WORD port, DWORD data);

    static bool shouldIgnorePort(WORD port);
    static void ignorePort(WORD port);

    QList<WORD> ports() const;

    enum { JunkValue = 0xff };

protected:
    enum ListenMask {
        ReadOnly = 1,
        WriteOnly = 2,
        ReadWrite = 3
    };
    virtual void listen(WORD port, ListenMask mask);

private:
    Machine& m_machine;
    const char* m_name { nullptr };
    int m_irq { 0 };
    QList<WORD> m_ports;

    static QSet<WORD> s_ignorePorts;
};

template<typename T>
inline T IODevice::in(WORD port)
{
    if (sizeof(T) == 1)
        return in8(port);
    if (sizeof(T) == 2)
        return in16(port);
    ASSERT(sizeof(T) == 4);
    return in32(port);
}

template<typename T>
inline void IODevice::out(WORD port, T data)
{
    if (sizeof(T) == 1)
        return out8(port, data);
    if (sizeof(T) == 2)
        return out16(port, data);
    ASSERT(sizeof(T) == 4);
    return out32(port, data);
}
