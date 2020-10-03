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

#include "CPU.h"

void CPU::_AAA(Instruction&)
{
    if (((getAL() & 0x0f) > 9) || getAF()) {
        setAX(getAX() + 0x0106);
        setAF(1);
        setCF(1);
    } else {
        setAF(0);
        setCF(0);
    }
    setAL(getAL() & 0x0f);
}

void CPU::_AAM(Instruction& insn)
{
    if (insn.imm8() == 0) {
        throw DivideError("AAM with 0 immediate");
    }

    u8 tempAL = getAL();
    setAH(tempAL / insn.imm8());
    setAL(tempAL % insn.imm8());
    updateFlags8(getAL());
    setAF(0);
}

void CPU::_AAD(Instruction& insn)
{
    u8 tempAL = getAL();
    u8 tempAH = getAH();

    setAL((tempAL + (tempAH * insn.imm8())) & 0xff);
    setAH(0x00);
    updateFlags8(getAL());
    setAF(0);
}

void CPU::_AAS(Instruction&)
{
    if (((getAL() & 0x0f) > 9) || getAF()) {
        setAX(getAX() - 6);
        setAH(getAH() - 1);
        setAF(1);
        setCF(1);
    } else {
        setAF(0);
        setCF(0);
    }
    setAL(getAL() & 0x0f);
}

void CPU::_DAS(Instruction&)
{
    bool oldCF = getCF();
    u8 oldAL = getAL();

    setCF(0);

    if (((getAL() & 0x0f) > 0x09) || getAF()) {
        setCF(((getAL() - 6) >> 8) & 1);
        setAL(getAL() - 0x06);
        setCF(oldCF | getCF());
        setAF(1);
    } else {
        setAF(0);
    }

    if (oldAL > 0x99 || oldCF == 1) {
        setAL(getAL() - 0x60);
        setCF(1);
    }

    updateFlags8(getAL());
}

void CPU::_DAA(Instruction&)
{
    bool oldCF = getCF();
    u8 oldAL = getAL();

    setCF(0);

    if (((getAL() & 0x0f) > 0x09) || getAF()) {
        setCF(((getAL() + 6) >> 8) & 1);
        setAL(getAL() + 6);
        setCF(oldCF | getCF());
        setAF(1);
    } else {
        setAF(0);
    }

    if (oldAL > 0x99 || oldCF == 1) {
        setAL(getAL() + 0x60);
        setCF(1);
    } else {
        setCF(0);
    }

    updateFlags8(getAL());
}
