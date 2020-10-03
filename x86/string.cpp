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
void CPU::doOnceOrRepeatedly(Instruction& insn, bool careAboutZF, F func)
{
    if (!insn.hasRepPrefix()) {
        func();
        return;
    }
    while (readRegisterForAddressSize(RegisterCX)) {
        if (getIF() && PIC::hasPendingIRQ() && !PIC::isIgnoringAllIRQs()) {
            throw HardwareInterruptDuringREP();
        }
        func();
        ++m_cycle;
        decrementCXForAddressSize();
        if (careAboutZF) {
            if (insn.repPrefix() == Prefix::REPZ && !getZF())
                break;
            if (insn.repPrefix() == Prefix::REPNZ && getZF())
                break;
        }
    }
}

template<typename T>
void CPU::doLODS(Instruction& insn)
{
    doOnceOrRepeatedly(insn, false, [this]() {
        writeRegister<T>(RegisterAL, readMemory<T>(currentSegment(), readRegisterForAddressSize(RegisterSI)));
        stepRegisterForAddressSize(RegisterSI, sizeof(T));
    });
}

template<typename T>
void CPU::doSTOS(Instruction& insn)
{
    doOnceOrRepeatedly(insn, false, [this]() {
        writeMemory<T>(SegmentRegisterIndex::ES, readRegisterForAddressSize(RegisterDI), readRegister<T>(RegisterAL));
        stepRegisterForAddressSize(RegisterDI, sizeof(T));
    });
}

template<typename T>
void CPU::doCMPS(Instruction& insn)
{
    typedef typename TypeDoubler<T>::type DT;
    doOnceOrRepeatedly(insn, true, [this]() {
        DT src = readMemory<T>(currentSegment(), readRegisterForAddressSize(RegisterSI));
        DT dest = readMemory<T>(SegmentRegisterIndex::ES, readRegisterForAddressSize(RegisterDI));
        stepRegisterForAddressSize(RegisterSI, sizeof(T));
        stepRegisterForAddressSize(RegisterDI, sizeof(T));
        cmpFlags<T>(src - dest, src, dest);
    });
}

template<typename T>
void CPU::doSCAS(Instruction& insn)
{
    typedef typename TypeDoubler<T>::type DT;
    doOnceOrRepeatedly(insn, true, [this]() {
        DT dest = readMemory<T>(SegmentRegisterIndex::ES, readRegisterForAddressSize(RegisterDI));
        stepRegisterForAddressSize(RegisterDI, sizeof(T));
        cmpFlags<T>(readRegister<T>(RegisterAL) - dest, readRegister<T>(RegisterAL), dest);
    });
}

template<typename T>
void CPU::doMOVS(Instruction& insn)
{
    doOnceOrRepeatedly(insn, false, [this]() {
        T tmp = readMemory<T>(currentSegment(), readRegisterForAddressSize(RegisterSI));
        writeMemory<T>(SegmentRegisterIndex::ES, readRegisterForAddressSize(RegisterDI), tmp);
        stepRegisterForAddressSize(RegisterSI, sizeof(T));
        stepRegisterForAddressSize(RegisterDI, sizeof(T));
    });
}

template<typename T>
void CPU::doOUTS(Instruction& insn)
{
    doOnceOrRepeatedly(insn, false, [this]() {
        T data = readMemory<T>(currentSegment(), readRegisterForAddressSize(RegisterSI));
        out<T>(getDX(), data);
        stepRegisterForAddressSize(RegisterSI, sizeof(T));
    });
}

template<typename T>
void CPU::doINS(Instruction& insn)
{
    doOnceOrRepeatedly(insn, false, [this]() {
        // FIXME: Should this really read the port without knowing that the destination memory is writable?
        T data = in<T>(getDX());
        writeMemory<T>(SegmentRegisterIndex::ES, readRegisterForAddressSize(RegisterDI), data);
        stepRegisterForAddressSize(RegisterDI, sizeof(T));
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
