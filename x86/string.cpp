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
#include "pic.h"

template<typename F>
void CPU::doOnceOrRepeatedly(Instruction& insn, bool care_about_zf, F func)
{
    if (!insn.has_rep_prefix()) {
        func();
        return;
    }
    while (read_register_for_address_size(RegisterCX)) {
        if (get_if() && PIC::has_pending_irq() && !PIC::is_ignoring_all_irqs()) {
            throw HardwareInterruptDuringREP();
        }
        func();
        ++m_cycle;
        decrement_cx_for_address_size();
        if (care_about_zf) {
            if (insn.rep_prefix() == Prefix::REPZ && !get_zf())
                break;
            if (insn.rep_prefix() == Prefix::REPNZ && get_zf())
                break;
        }
    }
}

template<typename T>
void CPU::doLODS(Instruction& insn)
{
    doOnceOrRepeatedly(insn, false, [this]() {
        write_register<T>(RegisterAL, read_memory<T>(current_segment(), read_register_for_address_size(RegisterSI)));
        step_register_for_address_size(RegisterSI, sizeof(T));
    });
}

template<typename T>
void CPU::doSTOS(Instruction& insn)
{
    doOnceOrRepeatedly(insn, false, [this]() {
        write_memory<T>(SegmentRegisterIndex::ES, read_register_for_address_size(RegisterDI), read_register<T>(RegisterAL));
        step_register_for_address_size(RegisterDI, sizeof(T));
    });
}

template<typename T>
void CPU::doCMPS(Instruction& insn)
{
    typedef typename TypeDoubler<T>::type DT;
    doOnceOrRepeatedly(insn, true, [this]() {
        DT src = read_memory<T>(current_segment(), read_register_for_address_size(RegisterSI));
        DT dest = read_memory<T>(SegmentRegisterIndex::ES, read_register_for_address_size(RegisterDI));
        step_register_for_address_size(RegisterSI, sizeof(T));
        step_register_for_address_size(RegisterDI, sizeof(T));
        cmp_flags<T>(src - dest, src, dest);
    });
}

template<typename T>
void CPU::doSCAS(Instruction& insn)
{
    typedef typename TypeDoubler<T>::type DT;
    doOnceOrRepeatedly(insn, true, [this]() {
        DT dest = read_memory<T>(SegmentRegisterIndex::ES, read_register_for_address_size(RegisterDI));
        step_register_for_address_size(RegisterDI, sizeof(T));
        cmp_flags<T>(read_register<T>(RegisterAL) - dest, read_register<T>(RegisterAL), dest);
    });
}

template<typename T>
void CPU::doMOVS(Instruction& insn)
{
    doOnceOrRepeatedly(insn, false, [this]() {
        T tmp = read_memory<T>(current_segment(), read_register_for_address_size(RegisterSI));
        write_memory<T>(SegmentRegisterIndex::ES, read_register_for_address_size(RegisterDI), tmp);
        step_register_for_address_size(RegisterSI, sizeof(T));
        step_register_for_address_size(RegisterDI, sizeof(T));
    });
}

template<typename T>
void CPU::doOUTS(Instruction& insn)
{
    doOnceOrRepeatedly(insn, false, [this]() {
        T data = read_memory<T>(current_segment(), read_register_for_address_size(RegisterSI));
        out<T>(get_dx(), data);
        step_register_for_address_size(RegisterSI, sizeof(T));
    });
}

template<typename T>
void CPU::doINS(Instruction& insn)
{
    doOnceOrRepeatedly(insn, false, [this]() {
        // FIXME: Should this really read the port without knowing that the destination memory is writable?
        T data = in<T>(get_dx());
        write_memory<T>(SegmentRegisterIndex::ES, read_register_for_address_size(RegisterDI), data);
        step_register_for_address_size(RegisterDI, sizeof(T));
    });
}

#define DEFINE_STRING_OP(basename)              \
    void CPU::_##basename##B(Instruction& insn) \
    {                                           \
        do                                      \
            ##basename<u8>(insn);               \
    }                                           \
    void CPU::_##basename##W(Instruction& insn) \
    {                                           \
        do                                      \
            ##basename<u16>(insn);              \
    }                                           \
    void CPU::_##basename##D(Instruction& insn) \
    {                                           \
        do                                      \
            ##basename<u32>(insn);              \
    }

DEFINE_STRING_OP(LODS)
DEFINE_STRING_OP(STOS)
DEFINE_STRING_OP(MOVS)
DEFINE_STRING_OP(OUTS)
DEFINE_STRING_OP(INS)
DEFINE_STRING_OP(CMPS)
DEFINE_STRING_OP(SCAS)
