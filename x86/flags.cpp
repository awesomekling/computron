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

bool CPU::get_pf() const
{
    if (m_dirty_flags & Flag::PF) {
        m_pf = 0x9669 << 2 >> ((m_last_result ^ m_last_result >> 4) & 0xf) & Flag::PF;
        m_dirty_flags &= ~Flag::PF;
    }
    return m_pf;
}

bool CPU::get_zf() const
{
    if (m_dirty_flags & Flag::ZF) {
        m_zf = (~m_last_result & (m_last_result - 1)) >> (m_last_op_size - 1) & 1;
        m_dirty_flags &= ~Flag::ZF;
    }
    return m_zf;
}

bool CPU::get_sf() const
{
    if (m_dirty_flags & Flag::SF) {
        m_sf = (m_last_result >> (m_last_op_size - 1)) & 1;
        m_dirty_flags &= ~Flag::SF;
    }
    return m_sf;
}

void CPU::update_flags32(u32 data)
{
    m_dirty_flags |= Flag::PF | Flag::ZF | Flag::SF;
    m_last_result = data;
    m_last_op_size = DWordSize;
}

void CPU::update_flags16(u16 data)
{
    m_dirty_flags |= Flag::PF | Flag::ZF | Flag::SF;
    m_last_result = data;
    m_last_op_size = WordSize;
}

void CPU::update_flags8(u8 data)
{
    m_dirty_flags |= Flag::PF | Flag::ZF | Flag::SF;
    m_last_result = data;
    m_last_op_size = ByteSize;
}

void CPU::_STC(Instruction&)
{
    set_cf(1);
}

void CPU::_STD(Instruction&)
{
    set_df(1);
}

void CPU::_STI(Instruction&)
{
    if (!get_pe() || get_iopl() >= get_cpl()) {
        if (get_if())
            make_next_instruction_uninterruptible();
        set_if(1);
        return;
    }

    if (!get_vme() && !get_pvi())
        throw GeneralProtectionFault(0, "STI with VME=0 && PVI=0");

    if (get_vip())
        throw GeneralProtectionFault(0, "STI with VIP=1");

    set_vif(1);
}

void CPU::_CLI(Instruction&)
{
    if (!get_pe() || get_iopl() >= get_cpl()) {
        set_if(0);
        return;
    }

    if (!get_vme() && !get_pvi())
        throw GeneralProtectionFault(0, "CLI with VME=0 && PVI=0");

    set_vif(0);
}

void CPU::_CLC(Instruction&)
{
    set_cf(0);
}

void CPU::_CLD(Instruction&)
{
    set_df(0);
}

void CPU::_CMC(Instruction&)
{
    set_cf(!get_cf());
}

void CPU::_LAHF(Instruction&)
{
    set_ah(get_cf() | (get_pf() * Flag::PF) | (get_af() * Flag::AF) | (get_zf() * Flag::ZF) | (get_sf() * Flag::SF) | 2);
}

void CPU::_SAHF(Instruction&)
{
    set_cf(get_ah() & Flag::CF);
    set_pf(get_ah() & Flag::PF);
    set_af(get_ah() & Flag::AF);
    set_zf(get_ah() & Flag::ZF);
    set_sf(get_ah() & Flag::SF);
}

void CPU::set_flags(u16 flags)
{
    set_cf(flags & Flag::CF);
    set_pf(flags & Flag::PF);
    set_af(flags & Flag::AF);
    set_zf(flags & Flag::ZF);
    set_sf(flags & Flag::SF);
    set_tf(flags & Flag::TF);
    set_if(flags & Flag::IF);
    set_df(flags & Flag::DF);
    set_of(flags & Flag::OF);
    set_iopl((flags & Flag::IOPL) >> 12);
    set_nt(flags & Flag::NT);
}

u16 CPU::get_flags() const
{
    return 0x0002
        | (get_cf() * Flag::CF)
        | (get_pf() * Flag::PF)
        | (get_af() * Flag::AF)
        | (get_zf() * Flag::ZF)
        | (get_sf() * Flag::SF)
        | (get_tf() * Flag::TF)
        | (get_if() * Flag::IF)
        | (get_df() * Flag::DF)
        | (get_of() * Flag::OF)
        | (get_iopl() << 12)
        | (get_nt() * Flag::NT);
}

void CPU::set_eflags(u32 eflags)
{
    set_flags(eflags & 0xffff);
    set_rf(eflags & Flag::RF);
    set_vm(eflags & Flag::VM);
    //    this->AC = (eflags & 0x40000) != 0;
    //    this->VIF = (eflags & 0x80000) != 0;
    //    this->VIP = (eflags & 0x100000) != 0;
    //    this->ID = (eflags & 0x200000) != 0;
}

u32 CPU::get_eflags() const
{
    u32 eflags = get_flags()
        | (this->m_rf * Flag::RF)
        | (this->m_vm * Flag::VM)
        //         | (this->AC << 18)
        //         | (this->VIF << 19)
        //         | (this->VIP << 20)
        //         | (this->ID << 21);
        ;
    return eflags;
}
