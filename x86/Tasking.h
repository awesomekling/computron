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
    TSS(CPU&, LinearAddress, bool is_32bit);

    bool is_32bit() const { return m_is_32bit; }

    u16 get_io_map_base() const;

    u32 get_ring_esp(u8) const;
    u16 get_ring_ss(u8) const;

    u32 get_esp0() const;
    u32 get_esp1() const;
    u32 get_esp2() const;
    u16 get_ss0() const;
    u16 get_ss1() const;
    u16 get_ss2() const;

    void set_ss0(u16);
    void set_ss1(u16);
    void set_ss2(u16);
    void set_esp0(u32);
    void set_esp1(u32);
    void set_esp2(u32);

    u16 get_backlink() const;
    u16 get_ldt() const;
    u32 get_eip() const;

    void set_cr3(u32);
    u32 get_cr3() const;

    u16 get_cs() const;
    u16 get_ds() const;
    u16 get_es() const;
    u16 get_ss() const;
    u16 get_fs() const;
    u16 get_gs() const;

    u32 get_eax() const;
    u32 get_ebx() const;
    u32 get_ecx() const;
    u32 get_edx() const;
    u32 get_esi() const;
    u32 get_edi() const;
    u32 get_esp() const;
    u32 get_ebp() const;
    u32 get_eflags() const;

    void set_cs(u16);
    void set_ds(u16);
    void set_es(u16);
    void set_ss(u16);
    void set_fs(u16);
    void set_gs(u16);
    void set_ldt(u16);
    void set_eip(u32);
    void set_eax(u32);
    void set_ebx(u32);
    void set_ecx(u32);
    void set_edx(u32);
    void set_ebp(u32);
    void set_esp(u32);
    void set_esi(u32);
    void set_edi(u32);
    void set_eflags(u32);
    void set_backlink(u16);

private:
    CPU& m_cpu;
    LinearAddress m_base;
    bool m_is_32bit { false };
};
