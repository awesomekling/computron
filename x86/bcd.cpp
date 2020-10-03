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
    if (((get_al() & 0x0f) > 9) || get_af()) {
        set_ax(get_ax() + 0x0106);
        set_af(1);
        set_cf(1);
    } else {
        set_af(0);
        set_cf(0);
    }
    set_al(get_al() & 0x0f);
}

void CPU::_AAM(Instruction& insn)
{
    if (insn.imm8() == 0) {
        throw DivideError("AAM with 0 immediate");
    }

    u8 tempAL = get_al();
    set_ah(tempAL / insn.imm8());
    set_al(tempAL % insn.imm8());
    update_flags8(get_al());
    set_af(0);
}

void CPU::_AAD(Instruction& insn)
{
    u8 tempAL = get_al();
    u8 tempAH = get_ah();

    set_al((tempAL + (tempAH * insn.imm8())) & 0xff);
    set_ah(0x00);
    update_flags8(get_al());
    set_af(0);
}

void CPU::_AAS(Instruction&)
{
    if (((get_al() & 0x0f) > 9) || get_af()) {
        set_ax(get_ax() - 6);
        set_ah(get_ah() - 1);
        set_af(1);
        set_cf(1);
    } else {
        set_af(0);
        set_cf(0);
    }
    set_al(get_al() & 0x0f);
}

void CPU::_DAS(Instruction&)
{
    bool oldCF = get_cf();
    u8 oldAL = get_al();

    set_cf(0);

    if (((get_al() & 0x0f) > 0x09) || get_af()) {
        set_cf(((get_al() - 6) >> 8) & 1);
        set_al(get_al() - 0x06);
        set_cf(oldCF | get_cf());
        set_af(1);
    } else {
        set_af(0);
    }

    if (oldAL > 0x99 || oldCF == 1) {
        set_al(get_al() - 0x60);
        set_cf(1);
    }

    update_flags8(get_al());
}

void CPU::_DAA(Instruction&)
{
    bool oldCF = get_cf();
    u8 oldAL = get_al();

    set_cf(0);

    if (((get_al() & 0x0f) > 0x09) || get_af()) {
        set_cf(((get_al() + 6) >> 8) & 1);
        set_al(get_al() + 6);
        set_cf(oldCF | get_cf());
        set_af(1);
    } else {
        set_af(0);
    }

    if (oldAL > 0x99 || oldCF == 1) {
        set_al(get_al() + 0x60);
        set_cf(1);
    } else {
        set_cf(0);
    }

    update_flags8(get_al());
}
