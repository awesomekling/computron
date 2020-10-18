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
    math_flags(result, dest, src);
    set_of(((
                ((result) ^ (dest)) & ((result) ^ (src)))
               >> (TypeTrivia<T>::bits - 1))
        & 1);
    return result;
}

template<typename T>
u64 CPU::doADC(T dest, T src)
{
    u64 result = (u64)dest + (u64)src + (u64)get_cf();

    math_flags(result, dest, src);
    set_of(((
                ((result) ^ (dest)) & ((result) ^ (src)))
               >> (TypeTrivia<T>::bits - 1))
        & 1);
    return result;
}

template<typename T>
u64 CPU::doSUB(T dest, T src)
{
    u64 result = (u64)dest - (u64)src;
    cmp_flags<T>(result, dest, src);
    return result;
}

template<typename T>
u64 CPU::doSBB(T dest, T src)
{
    u64 result = (u64)dest - (u64)src - (u64)get_cf();
    cmp_flags<T>(result, dest, src);
    return result;
}

DEFINE_INSTRUCTION_HANDLERS_GRP1(ADD)
DEFINE_INSTRUCTION_HANDLERS_GRP1(ADC)
DEFINE_INSTRUCTION_HANDLERS_GRP1(SUB)
DEFINE_INSTRUCTION_HANDLERS_GRP1(SBB)
DEFINE_INSTRUCTION_HANDLERS_GRP4_READONLY(SUB, CMP)

template<typename T>
void CPU::doMUL(T f1, T f2, T& result_high, T& result_low)
{
    typedef typename TypeDoubler<T>::type DT;
    DT result = (DT)f1 * (DT)f2;
    result_low = result & TypeTrivia<T>::mask;
    result_high = (result >> TypeTrivia<T>::bits) & TypeTrivia<T>::mask;

    if (result_high == 0) {
        set_cf(0);
        set_of(0);
    } else {
        set_cf(1);
        set_of(1);
    }
}

void CPU::_MUL_RM8(Instruction& insn)
{
    doMUL<u8>(get_al(), insn.modrm().read8(), mutable_reg8(RegisterAH), mutable_reg8(RegisterAL));
}

void CPU::_MUL_RM16(Instruction& insn)
{
    doMUL<u16>(get_ax(), insn.modrm().read16(), mutable_reg16(RegisterDX), mutable_reg16(RegisterAX));
}

void CPU::_MUL_RM32(Instruction& insn)
{
    doMUL<u32>(get_eax(), insn.modrm().read32(), mutable_reg32(RegisterEDX), mutable_reg32(RegisterEAX));
}

template<typename T>
void CPU::doIMUL(T f1, T f2, T& result_high, T& result_low)
{
    typedef typename TypeDoubler<T>::type DT;
    DT result = (DT)f1 * (DT)f2;
    result_low = result & TypeTrivia<T>::mask;
    result_high = (result >> TypeTrivia<T>::bits) & TypeTrivia<T>::mask;

    if (result > std::numeric_limits<T>::max() || result < std::numeric_limits<T>::min()) {
        set_cf(1);
        set_of(1);
    } else {
        set_cf(0);
        set_of(0);
    }
}

void CPU::_IMUL_RM8(Instruction& insn)
{
    doIMUL<i8>(insn.modrm().read8(), get_al(), (i8&)mutable_reg8(RegisterAH), (i8&)mutable_reg8(RegisterAL));
}

void CPU::_IMUL_reg32_RM32_imm8(Instruction& insn)
{
    i32 result_high;
    doIMUL<i32>(insn.modrm().read32(), signExtendedTo<i32>(insn.imm8()), result_high, (i32&)insn.reg32());
}

void CPU::_IMUL_reg32_RM32_imm32(Instruction& insn)
{
    i32 result_high;
    doIMUL<i32>(insn.modrm().read32(), insn.imm32(), result_high, (i32&)insn.reg32());
}

void CPU::_IMUL_reg16_RM16_imm16(Instruction& insn)
{
    i16 result_high;
    doIMUL<i16>(insn.modrm().read16(), insn.imm16(), result_high, (i16&)insn.reg16());
}

void CPU::_IMUL_reg16_RM16(Instruction& insn)
{
    i16 result_high;
    doIMUL<i16>(insn.reg16(), insn.modrm().read16(), result_high, (i16&)insn.reg16());
}

void CPU::_IMUL_reg32_RM32(Instruction& insn)
{
    i32 result_high;
    doIMUL<i32>(insn.reg32(), insn.modrm().read32(), result_high, (i32&)insn.reg32());
}

void CPU::_IMUL_reg16_RM16_imm8(Instruction& insn)
{
    i16 result_high;
    doIMUL<i16>(insn.modrm().read16(), signExtendedTo<i16>(insn.imm8()), result_high, (i16&)insn.reg16());
}

void CPU::_IMUL_RM16(Instruction& insn)
{
    doIMUL<i16>(insn.modrm().read16(), get_ax(), (i16&)mutable_reg16(RegisterDX), (i16&)mutable_reg16(RegisterAX));
}

void CPU::_IMUL_RM32(Instruction& insn)
{
    doIMUL<i32>(insn.modrm().read32(), get_eax(), (i32&)mutable_reg32(RegisterEDX), (i32&)mutable_reg32(RegisterEAX));
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
    doDIV<u8>(get_ah(), get_al(), insn.modrm().read8(), mutable_reg8(RegisterAL), mutable_reg8(RegisterAH));
}

void CPU::_DIV_RM16(Instruction& insn)
{
    doDIV<u16>(get_dx(), get_ax(), insn.modrm().read16(), mutable_reg16(RegisterAX), mutable_reg16(RegisterDX));
}

void CPU::_DIV_RM32(Instruction& insn)
{
    doDIV<u32>(get_edx(), get_eax(), insn.modrm().read32(), mutable_reg32(RegisterEAX), mutable_reg32(RegisterEDX));
}

void CPU::_IDIV_RM8(Instruction& insn)
{
    doDIV<i8>(get_ah(), get_al(), insn.modrm().read8(), (i8&)mutable_reg8(RegisterAL), (i8&)mutable_reg8(RegisterAH));
}

void CPU::_IDIV_RM16(Instruction& insn)
{
    doDIV<i16>(get_dx(), get_ax(), insn.modrm().read16(), (i16&)mutable_reg16(RegisterAX), (i16&)mutable_reg16(RegisterDX));
}

void CPU::_IDIV_RM32(Instruction& insn)
{
    doDIV<i32>(get_edx(), get_eax(), insn.modrm().read32(), (i32&)mutable_reg32(RegisterEAX), (i32&)mutable_reg32(RegisterEDX));
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

void CPU::_XADD_RM16_reg16(Instruction& insn)
{
    auto dest = insn.modrm().read16();
    auto src = insn.reg16();

    auto result = doADD(dest, src);
    insn.reg16() = dest;
    insn.modrm().write16(result);
}

void CPU::_XADD_RM32_reg32(Instruction& insn)
{
    auto dest = insn.modrm().read32();
    auto src = insn.reg32();
    auto result = doADD(dest, src);
    insn.reg32() = dest;
    insn.modrm().write32(result);
}

void CPU::_XADD_RM8_reg8(Instruction& insn)
{
    auto dest = insn.modrm().read8();
    auto src = insn.reg8();
    auto result = doADD(dest, src);
    insn.reg8() = dest;
    insn.modrm().write8(result);
}
