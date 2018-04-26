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
#include "templates.h"

template<typename T>
QWORD CPU::doADD(T dest, T src)
{
    QWORD result = (QWORD)dest + (QWORD)src;
    mathFlags(result, dest, src);
    setOF(((
          ((result)^(dest)) &
          ((result)^(src))
         )>>(TypeTrivia<T>::bits - 1))&1);
    return result;
}

template<typename T>
QWORD CPU::doADC(T dest, T src)
{
    QWORD result = (QWORD)dest + (QWORD)src + (QWORD)getCF();

    mathFlags(result, dest, src);
    setOF(((
          ((result)^(dest)) &
          ((result)^(src))
         )>>(TypeTrivia<T>::bits - 1))&1);
    return result;
}

template<typename T>
QWORD CPU::doSUB(T dest, T src)
{
    QWORD result = (QWORD)dest - (QWORD)src;
    cmpFlags<T>(result, dest, src);
    return result;
}

template<typename T>
QWORD CPU::doSBB(T dest, T src)
{
    QWORD result = (QWORD)dest - (QWORD)src - (QWORD)getCF();
    cmpFlags<T>(result, dest, src);
    return result;
}

DEFINE_INSTRUCTION_HANDLERS_GRP1(ADD)
DEFINE_INSTRUCTION_HANDLERS_GRP1(ADC)
DEFINE_INSTRUCTION_HANDLERS_GRP1(SUB)
DEFINE_INSTRUCTION_HANDLERS_GRP1(SBB)
DEFINE_INSTRUCTION_HANDLERS_GRP4_READONLY(SUB, CMP)

template<typename T>
void CPU::doMUL(T f1, T f2, T& resultHigh, T& resultLow)
{
    typedef typename TypeDoubler<T>::type DT;
    DT result = (DT)f1 * (DT)f2;
    resultLow = result & TypeTrivia<T>::mask;
    resultHigh = (result >> TypeTrivia<T>::bits) & TypeTrivia<T>::mask;

    if (resultHigh == 0) {
        setCF(0);
        setOF(0);
    } else {
        setCF(1);
        setOF(1);
    }
}

void CPU::_MUL_RM8(Instruction& insn)
{
    doMUL<BYTE>(getAL(), insn.modrm().read8(), mutableReg8(RegisterAH), mutableReg8(RegisterAL));
}

void CPU::_MUL_RM16(Instruction& insn)
{
    doMUL<WORD>(getAX(), insn.modrm().read16(), mutableReg16(RegisterDX), mutableReg16(RegisterAX));
}

void CPU::_MUL_RM32(Instruction& insn)
{
    doMUL<DWORD>(getEAX(), insn.modrm().read32(), mutableReg32(RegisterEDX), mutableReg32(RegisterEAX));
}

template<typename T>
void CPU::doIMUL(T f1, T f2, T& resultHigh, T& resultLow)
{
    typedef typename TypeDoubler<T>::type DT;
    DT result = (DT)f1 * (DT)f2;
    resultLow = result & TypeTrivia<T>::mask;
    resultHigh = (result >> TypeTrivia<T>::bits) & TypeTrivia<T>::mask;

    if (result > std::numeric_limits<T>::max() || result < std::numeric_limits<T>::min()) {
        setCF(1);
        setOF(1);
    } else {
        setCF(0);
        setOF(0);
    }
}

void CPU::_IMUL_RM8(Instruction& insn)
{
    doIMUL<SIGNED_BYTE>(insn.modrm().read8(), getAL(), (SIGNED_BYTE&)mutableReg8(RegisterAH), (SIGNED_BYTE&)mutableReg8(RegisterAL));
}

void CPU::_IMUL_reg32_RM32_imm8(Instruction& insn)
{
    SIGNED_DWORD resultHigh;
    doIMUL<SIGNED_DWORD>(insn.modrm().read32(), signExtendedTo<SIGNED_DWORD>(insn.imm8()), resultHigh, (SIGNED_DWORD&)insn.reg32());
}

void CPU::_IMUL_reg32_RM32_imm32(Instruction& insn)
{
    SIGNED_DWORD resultHigh;
    doIMUL<SIGNED_DWORD>(insn.modrm().read32(), insn.imm32(), resultHigh, (SIGNED_DWORD&)insn.reg32());
}

void CPU::_IMUL_reg16_RM16_imm16(Instruction& insn)
{
    SIGNED_WORD resultHigh;
    doIMUL<SIGNED_WORD>(insn.modrm().read16(), insn.imm16(), resultHigh, (SIGNED_WORD&)insn.reg16());
}

void CPU::_IMUL_reg16_RM16(Instruction& insn)
{
    SIGNED_WORD resultHigh;
    doIMUL<SIGNED_WORD>(insn.reg16(), insn.modrm().read16(), resultHigh, (SIGNED_WORD&)insn.reg16());
}

void CPU::_IMUL_reg32_RM32(Instruction& insn)
{
    SIGNED_DWORD resultHigh;
    doIMUL<SIGNED_DWORD>(insn.reg32(), insn.modrm().read32(), resultHigh, (SIGNED_DWORD&)insn.reg32());
}

void CPU::_IMUL_reg16_RM16_imm8(Instruction& insn)
{
    SIGNED_WORD resultHigh;
    doIMUL<SIGNED_WORD>(insn.modrm().read16(), signExtendedTo<SIGNED_WORD>(insn.imm8()), resultHigh, (SIGNED_WORD&)insn.reg16());
}

void CPU::_IMUL_RM16(Instruction& insn)
{
    doIMUL<SIGNED_WORD>(insn.modrm().read16(), getAX(), (SIGNED_WORD&)mutableReg16(RegisterDX), (SIGNED_WORD&)mutableReg16(RegisterAX));
}

void CPU::_IMUL_RM32(Instruction& insn)
{
    doIMUL<SIGNED_DWORD>(insn.modrm().read32(), getEAX(), (SIGNED_DWORD&)mutableReg32(RegisterEDX), (SIGNED_DWORD&)mutableReg32(RegisterEAX));
}

template<typename T>
void CPU::doDIV(T dividendHigh, T dividendLow, T divisor, T& quotient, T& remainder)
{
    if (divisor == 0) {
        throw DivideError("Divide by zero");
    }

    typedef typename TypeDoubler<T>::type DT;
    DT dividend = weld<DT>(dividendHigh, dividendLow);
    DT result = dividend / divisor;
    if (result > std::numeric_limits<T>::max() || result < std::numeric_limits<T>::min()) {
        throw DivideError(QString("Divide overflow (%1 / %2 = %3 { range = %4 - %5 })").arg(dividend).arg(divisor).arg(result).arg(std::numeric_limits<T>::min()).arg(std::numeric_limits<T>::max()));
    }

    quotient = result;
    remainder = dividend % divisor;
}

void CPU::_DIV_RM8(Instruction& insn)
{
    doDIV<BYTE>(getAH(), getAL(), insn.modrm().read8(), mutableReg8(RegisterAL), mutableReg8(RegisterAH));
}

void CPU::_DIV_RM16(Instruction& insn)
{
    doDIV<WORD>(getDX(), getAX(), insn.modrm().read16(), mutableReg16(RegisterAX), mutableReg16(RegisterDX));
}

void CPU::_DIV_RM32(Instruction& insn)
{
    doDIV<DWORD>(getEDX(), getEAX(), insn.modrm().read32(), mutableReg32(RegisterEAX), mutableReg32(RegisterEDX));
}

void CPU::_IDIV_RM8(Instruction& insn)
{
    doDIV<SIGNED_BYTE>(getAH(), getAL(), insn.modrm().read8(), (SIGNED_BYTE&)mutableReg8(RegisterAL), (SIGNED_BYTE&)mutableReg8(RegisterAH));
}

void CPU::_IDIV_RM16(Instruction& insn)
{
    doDIV<SIGNED_WORD>(getDX(), getAX(), insn.modrm().read16(), (SIGNED_WORD&)mutableReg16(RegisterAX), (SIGNED_WORD&)mutableReg16(RegisterDX));
}

void CPU::_IDIV_RM32(Instruction& insn)
{
    doDIV<SIGNED_DWORD>(getEDX(), getEAX(), insn.modrm().read32(), (SIGNED_DWORD&)mutableReg32(RegisterEAX), (SIGNED_DWORD&)mutableReg32(RegisterEDX));
}

template<typename T>
void CPU::doNEG(Instruction& insn)
{
    insn.modrm().write<T>(doSUB((T)0, insn.modrm().read<T>()));
}

void CPU::_NEG_RM8(Instruction& insn)
{
    doNEG<BYTE>(insn);
}

void CPU::_NEG_RM16(Instruction& insn)
{
    doNEG<WORD>(insn);
}

void CPU::_NEG_RM32(Instruction& insn)
{
    doNEG<DWORD>(insn);
}
