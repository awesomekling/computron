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
u64 CPU::doADD(T dest, T src)
{
    u64 result = (u64)dest + (u64)src;
    mathFlags(result, dest, src);
    setOF(((
               ((result) ^ (dest)) & ((result) ^ (src)))
              >> (TypeTrivia<T>::bits - 1))
        & 1);
    return result;
}

template<typename T>
u64 CPU::doADC(T dest, T src)
{
    u64 result = (u64)dest + (u64)src + (u64)getCF();

    mathFlags(result, dest, src);
    setOF(((
               ((result) ^ (dest)) & ((result) ^ (src)))
              >> (TypeTrivia<T>::bits - 1))
        & 1);
    return result;
}

template<typename T>
u64 CPU::doSUB(T dest, T src)
{
    u64 result = (u64)dest - (u64)src;
    cmpFlags<T>(result, dest, src);
    return result;
}

template<typename T>
u64 CPU::doSBB(T dest, T src)
{
    u64 result = (u64)dest - (u64)src - (u64)getCF();
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
    doMUL<u8>(getAL(), insn.modrm().read8(), mutableReg8(RegisterAH), mutableReg8(RegisterAL));
}

void CPU::_MUL_RM16(Instruction& insn)
{
    doMUL<u16>(getAX(), insn.modrm().read16(), mutableReg16(RegisterDX), mutableReg16(RegisterAX));
}

void CPU::_MUL_RM32(Instruction& insn)
{
    doMUL<u32>(getEAX(), insn.modrm().read32(), mutableReg32(RegisterEDX), mutableReg32(RegisterEAX));
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
    doIMUL<i8>(insn.modrm().read8(), getAL(), (i8&)mutableReg8(RegisterAH), (i8&)mutableReg8(RegisterAL));
}

void CPU::_IMUL_reg32_RM32_imm8(Instruction& insn)
{
    i32 resultHigh;
    doIMUL<i32>(insn.modrm().read32(), signExtendedTo<i32>(insn.imm8()), resultHigh, (i32&)insn.reg32());
}

void CPU::_IMUL_reg32_RM32_imm32(Instruction& insn)
{
    i32 resultHigh;
    doIMUL<i32>(insn.modrm().read32(), insn.imm32(), resultHigh, (i32&)insn.reg32());
}

void CPU::_IMUL_reg16_RM16_imm16(Instruction& insn)
{
    i16 resultHigh;
    doIMUL<i16>(insn.modrm().read16(), insn.imm16(), resultHigh, (i16&)insn.reg16());
}

void CPU::_IMUL_reg16_RM16(Instruction& insn)
{
    i16 resultHigh;
    doIMUL<i16>(insn.reg16(), insn.modrm().read16(), resultHigh, (i16&)insn.reg16());
}

void CPU::_IMUL_reg32_RM32(Instruction& insn)
{
    i32 resultHigh;
    doIMUL<i32>(insn.reg32(), insn.modrm().read32(), resultHigh, (i32&)insn.reg32());
}

void CPU::_IMUL_reg16_RM16_imm8(Instruction& insn)
{
    i16 resultHigh;
    doIMUL<i16>(insn.modrm().read16(), signExtendedTo<i16>(insn.imm8()), resultHigh, (i16&)insn.reg16());
}

void CPU::_IMUL_RM16(Instruction& insn)
{
    doIMUL<i16>(insn.modrm().read16(), getAX(), (i16&)mutableReg16(RegisterDX), (i16&)mutableReg16(RegisterAX));
}

void CPU::_IMUL_RM32(Instruction& insn)
{
    doIMUL<i32>(insn.modrm().read32(), getEAX(), (i32&)mutableReg32(RegisterEDX), (i32&)mutableReg32(RegisterEAX));
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
    doDIV<u8>(getAH(), getAL(), insn.modrm().read8(), mutableReg8(RegisterAL), mutableReg8(RegisterAH));
}

void CPU::_DIV_RM16(Instruction& insn)
{
    doDIV<u16>(getDX(), getAX(), insn.modrm().read16(), mutableReg16(RegisterAX), mutableReg16(RegisterDX));
}

void CPU::_DIV_RM32(Instruction& insn)
{
    doDIV<u32>(getEDX(), getEAX(), insn.modrm().read32(), mutableReg32(RegisterEAX), mutableReg32(RegisterEDX));
}

void CPU::_IDIV_RM8(Instruction& insn)
{
    doDIV<i8>(getAH(), getAL(), insn.modrm().read8(), (i8&)mutableReg8(RegisterAL), (i8&)mutableReg8(RegisterAH));
}

void CPU::_IDIV_RM16(Instruction& insn)
{
    doDIV<i16>(getDX(), getAX(), insn.modrm().read16(), (i16&)mutableReg16(RegisterAX), (i16&)mutableReg16(RegisterDX));
}

void CPU::_IDIV_RM32(Instruction& insn)
{
    doDIV<i32>(getEDX(), getEAX(), insn.modrm().read32(), (i32&)mutableReg32(RegisterEAX), (i32&)mutableReg32(RegisterEDX));
}

template<typename T>
void CPU::doNEG(Instruction& insn)
{
    insn.modrm().write<T>(doSUB((T)0, insn.modrm().read<T>()));
}

void CPU::_NEG_RM8(Instruction& insn)
{
    doNEG<u8>(insn);
}

void CPU::_NEG_RM16(Instruction& insn)
{
    doNEG<u16>(insn);
}

void CPU::_NEG_RM32(Instruction& insn)
{
    doNEG<u32>(insn);
}
