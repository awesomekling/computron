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

#include "Common.h"
#include "debug.h"

class CPU;

class TSS {
public:
    TSS(CPU&, LinearAddress, bool is32Bit);

    bool is32Bit() const { return m_is32Bit; }

    u16 getIOMapBase() const;

    u32 getRingESP(u8) const;
    u16 getRingSS(u8) const;

    u32 getESP0() const;
    u32 getESP1() const;
    u32 getESP2() const;
    u16 getSS0() const;
    u16 getSS1() const;
    u16 getSS2() const;

    void setSS0(u16);
    void setSS1(u16);
    void setSS2(u16);
    void setESP0(u32);
    void setESP1(u32);
    void setESP2(u32);

    u16 getBacklink() const;
    u16 getLDT() const;
    u32 getEIP() const;

    void setCR3(u32);
    u32 getCR3() const;

    u16 getCS() const;
    u16 getDS() const;
    u16 getES() const;
    u16 getSS() const;
    u16 getFS() const;
    u16 getGS() const;

    u32 getEAX() const;
    u32 getEBX() const;
    u32 getECX() const;
    u32 getEDX() const;
    u32 getESI() const;
    u32 getEDI() const;
    u32 getESP() const;
    u32 getEBP() const;
    u32 getEFlags() const;

    void setCS(u16);
    void setDS(u16);
    void setES(u16);
    void setSS(u16);
    void setFS(u16);
    void setGS(u16);
    void setLDT(u16);
    void setEIP(u32);
    void setEAX(u32);
    void setEBX(u32);
    void setECX(u32);
    void setEDX(u32);
    void setEBP(u32);
    void setESP(u32);
    void setESI(u32);
    void setEDI(u32);
    void setEFlags(u32);
    void setBacklink(u16);

private:
    CPU& m_cpu;
    LinearAddress m_base;
    bool m_is32Bit { false };
};
