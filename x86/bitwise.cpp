// Computron x86 PC Emulator
// Copyright (C) 2003-2019 Andreas Kling <awesomekling@gmail.com>
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

DEFINE_INSTRUCTION_HANDLERS_GRP1(AND)
DEFINE_INSTRUCTION_HANDLERS_GRP1(XOR)
DEFINE_INSTRUCTION_HANDLERS_GRP1(OR)
DEFINE_INSTRUCTION_HANDLERS_GRP5_READONLY(AND, TEST)

DEFINE_INSTRUCTION_HANDLERS_GRP3(ROL)
DEFINE_INSTRUCTION_HANDLERS_GRP3(ROR)
DEFINE_INSTRUCTION_HANDLERS_GRP3(SHL)
DEFINE_INSTRUCTION_HANDLERS_GRP3(SHR)
DEFINE_INSTRUCTION_HANDLERS_GRP3(SAR)
DEFINE_INSTRUCTION_HANDLERS_GRP3(RCL)
DEFINE_INSTRUCTION_HANDLERS_GRP3(RCR)

void CPU::_CBW(Instruction&)
{
    if (get_al() & 0x80)
        set_ah(0xFF);
    else
        set_ah(0x00);
}

void CPU::_CWD(Instruction&)
{
    if (get_ax() & 0x8000)
        set_dx(0xFFFF);
    else
        set_dx(0x0000);
}

void CPU::_CWDE(Instruction&)
{
    set_eax(signExtendedTo<u32>(get_ax()));
}

void CPU::_CDQ(Instruction&)
{
    if (get_eax() & 0x80000000)
        set_edx(0xFFFFFFFF);
    else
        set_edx(0x00000000);
}

void CPU::_SALC(Instruction&)
{
    set_al(get_cf() ? 0xFF : 0);
}

template<typename T>
T CPU::doOR(T dest, T src)
{
    T result = dest | src;
    update_flags<T>(result);
    set_of(0);
    set_cf(0);
    return result;
}

template<typename T>
T CPU::doXOR(T dest, T src)
{
    T result = dest ^ src;
    update_flags<T>(result);
    set_of(0);
    set_cf(0);
    return result;
}

template<typename T>
T CPU::doAND(T dest, T src)
{
    T result = dest & src;
    update_flags<T>(result);
    set_of(0);
    set_cf(0);
    return result;
}

template<typename T>
T CPU::doROL(T data, unsigned steps)
{
    T result = data;
    steps &= 0x1f;
    if (!steps)
        return data;

    steps &= TypeTrivia<T>::bits - 1;
    result = (data << steps) | (data >> (TypeTrivia<T>::bits - steps));
    set_cf(result & 1);
    set_of(((result >> (TypeTrivia<T>::bits - 1)) & 1) ^ get_cf());

    return result;
}

template<typename T>
T CPU::doROR(T data, unsigned steps)
{
    steps &= 0x1f;
    if (!steps)
        return data;

    T result = data;
    steps &= TypeTrivia<T>::bits - 1;
    result = (data >> steps) | (data << (TypeTrivia<T>::bits - steps));
    set_cf((result >> (TypeTrivia<T>::bits - 1)) & 1);
    set_of((result >> (TypeTrivia<T>::bits - 1)) ^ ((result >> (TypeTrivia<T>::bits - 2) & 1)));
    return result;
}

template<typename T>
T CPU::doSHL(T data, unsigned steps)
{
    T result = data;
    steps &= 0x1F;
    if (!steps)
        return data;

    if (steps <= TypeTrivia<T>::bits) {
        set_cf(result >> (TypeTrivia<T>::bits - steps) & 1);
    }
    result <<= steps;
    set_of((result >> (TypeTrivia<T>::bits - 1)) ^ get_cf());
    update_flags<T>(result);
    return result;
}

template<typename T>
T CPU::doSHR(T data, unsigned steps)
{
    T result = data;
    steps &= 0x1F;
    if (!steps)
        return data;

    if (steps <= TypeTrivia<T>::bits) {
        set_cf((result >> (steps - 1)) & 1);
        set_of((data >> (TypeTrivia<T>::bits - 1)) & 1);
    }
    result >>= steps;

    update_flags<T>(result);
    return result;
}

template<typename T>
T CPU::doSAR(T data, unsigned steps)
{
    // FIXME: This is painfully unoptimized.
    steps &= 0x1f;
    if (!steps)
        return data;

    T result = data;
    T mask = 1 << (TypeTrivia<T>::bits - 1);

    for (unsigned i = 0; i < steps; ++i) {
        T n = result;
        result = (result >> 1) | (n & mask);
        set_cf(n & 1);
    }
    set_of(0);
    update_flags<T>(result);
    return result;
}

template<typename T>
inline T allOnes()
{
    if (TypeTrivia<T>::bits == 8)
        return 0xff;
    if (TypeTrivia<T>::bits == 16)
        return 0xffff;
    if (TypeTrivia<T>::bits == 32)
        return 0xffffffff;
}

template<typename T>
T CPU::doRCL(T data, unsigned steps)
{
    // FIXME: This is painfully unoptimized.
    T result = data;
    T mask = allOnes<T>();
    steps &= 0x1f;
    if (!steps)
        return data;

    for (unsigned i = 0; i < steps; ++i) {
        T n = result;
        result = ((result << 1) & mask) | get_cf();
        set_cf((n >> (TypeTrivia<T>::bits - 1)) & 1);
    }
    set_of((result >> (TypeTrivia<T>::bits - 1)) ^ get_cf());
    return result;
}

template<typename T>
T CPU::doRCR(T data, unsigned steps)
{
    // FIXME: This is painfully unoptimized.
    T result = data;
    steps &= 0x1f;
    if (!steps)
        return data;

    for (unsigned i = 0; i < steps; ++i) {
        T n = result;
        result = (result >> 1) | (get_cf() << (TypeTrivia<T>::bits - 1));
        set_cf(n & 1);
    }
    set_of((result >> (TypeTrivia<T>::bits - 1)) ^ ((result >> (TypeTrivia<T>::bits - 2) & 1)));
    return result;
}

template<typename T>
void CPU::doNOT(Instruction& insn)
{
    insn.modrm().write<T>(~insn.modrm().read<T>());
}

void CPU::_NOT_RM8(Instruction& insn)
{
    doNOT<u8>(insn);
}

void CPU::_NOT_RM16(Instruction& insn)
{
    doNOT<u16>(insn);
}

void CPU::_NOT_RM32(Instruction& insn)
{
    doNOT<u32>(insn);
}

struct op_BT {
    static bool should_update() { return false; }
    template<typename T>
    static T op(T original, T) { return original; }
};
struct op_BTS {
    static bool should_update() { return true; }
    template<typename T>
    static T op(T original, T bit_mask) { return original | bit_mask; }
};
struct op_BTR {
    static bool should_update() { return true; }
    template<typename T>
    static T op(T original, T bit_mask) { return original & ~bit_mask; }
};
struct op_BTC {
    static bool should_update() { return true; }
    template<typename T>
    static T op(T original, T bit_mask) { return original ^ bit_mask; }
};

template<typename BTx_Op, typename T>
void CPU::_BTx_RM_imm8(Instruction& insn)
{
    auto& modrm = insn.modrm();
    unsigned bit_index = insn.imm8() & (TypeTrivia<T>::bits - 1);
    T original = modrm.read<T>();
    T bit_mask = 1 << bit_index;
    T result = BTx_Op::op(original, bit_mask);
    set_cf((original & bit_mask) != 0);
    if (BTx_Op::should_update())
        modrm.write(result);
}

template<typename BTx_Op>
void CPU::_BTx_RM32_imm8(Instruction& insn)
{
    _BTx_RM_imm8<BTx_Op, u32>(insn);
}

template<typename BTx_Op>
void CPU::_BTx_RM16_imm8(Instruction& insn)
{
    _BTx_RM_imm8<BTx_Op, u16>(insn);
}

template<typename BTx_Op>
void CPU::_BTx_RM32_reg32(Instruction& insn)
{
    _BTx_RM_reg<BTx_Op, u32>(insn);
}

template<typename BTx_Op>
void CPU::_BTx_RM16_reg16(Instruction& insn)
{
    _BTx_RM_reg<BTx_Op, u16>(insn);
}

#define DEFINE_INSTRUCTION_HANDLERS_FOR_BTx_OP(op)  \
    void CPU::_##op##_RM32_reg32(Instruction& insn) \
    {                                               \
        _BTx_RM32_reg32<op_##op>(insn);             \
    }                                               \
    void CPU::_##op##_RM16_reg16(Instruction& insn) \
    {                                               \
        _BTx_RM16_reg16<op_##op>(insn);             \
    }                                               \
    void CPU::_##op##_RM32_imm8(Instruction& insn)  \
    {                                               \
        _BTx_RM32_imm8<op_##op>(insn);              \
    }                                               \
    void CPU::_##op##_RM16_imm8(Instruction& insn)  \
    {                                               \
        _BTx_RM16_imm8<op_##op>(insn);              \
    }

DEFINE_INSTRUCTION_HANDLERS_FOR_BTx_OP(BTS)
    DEFINE_INSTRUCTION_HANDLERS_FOR_BTx_OP(BTR)
        DEFINE_INSTRUCTION_HANDLERS_FOR_BTx_OP(BTC)
            DEFINE_INSTRUCTION_HANDLERS_FOR_BTx_OP(BT)

                template<typename BTx_Op, typename T>
                void CPU::_BTx_RM_reg(Instruction& insn)
{
    auto& modrm = insn.modrm();
    if (modrm.is_register()) {
        unsigned bit_index = insn.reg<T>() & (TypeTrivia<T>::bits - 1);
        T original = modrm.read<T>();
        T bit_mask = 1 << bit_index;
        T result = BTx_Op::op(original, bit_mask);
        set_cf((original & bit_mask) != 0);
        if (BTx_Op::should_update())
            modrm.write(result);
        return;
    }
    // FIXME: Maybe this should do 32-bit r/m/w?
    unsigned bit_offset_in_array = insn.reg<T>() / 8;
    unsigned bit_offset_in_byte = insn.reg<T>() & 7;
    LinearAddress laddr(modrm.offset() + bit_offset_in_array);
    u8 dest = read_memory8(laddr);
    u8 bit_mask = 1 << bit_offset_in_byte;
    u8 result = BTx_Op::op(dest, bit_mask);
    set_cf((dest & bit_mask) != 0);
    if (BTx_Op::should_update())
        write_memory8(laddr, result);
}

template<typename T>
T CPU::doBSF(T src)
{
    set_zf(src == 0);
    if (!src)
        return 0;
    for (unsigned i = 0; i < TypeTrivia<T>::bits; ++i) {
        T mask = 1 << i;
        if (src & mask)
            return i;
    }
    ASSERT_NOT_REACHED();
    return 0;
}

template<typename T>
T CPU::doBSR(T src)
{
    set_zf(src == 0);
    if (!src)
        return 0;
    for (int i = TypeTrivia<T>::bits - 1; i >= 0; --i) {
        T mask = 1 << i;
        if (src & mask)
            return i;
    }
    ASSERT_NOT_REACHED();
    return 0;
}

void CPU::_BSF_reg16_RM16(Instruction& insn)
{
    insn.reg16() = doBSF(insn.modrm().read16());
}

void CPU::_BSF_reg32_RM32(Instruction& insn)
{
    insn.reg32() = doBSF(insn.modrm().read32());
}

void CPU::_BSR_reg16_RM16(Instruction& insn)
{
    insn.reg16() = doBSR(insn.modrm().read16());
}

void CPU::_BSR_reg32_RM32(Instruction& insn)
{
    insn.reg32() = doBSR(insn.modrm().read32());
}

template<typename T>
T CPU::doSHLD(T leftData, T rightData, unsigned steps)
{
    steps &= 31;
    if (!steps)
        return leftData;

    T result;

    if (steps > TypeTrivia<T>::bits) {
        result = (leftData >> ((TypeTrivia<T>::bits * 2) - steps) | (rightData << (steps - TypeTrivia<T>::bits)));
        set_cf((rightData >> ((TypeTrivia<T>::bits * 2) - steps)) & 1);
    } else {
        result = (leftData << steps) | (rightData >> (TypeTrivia<T>::bits - steps));
        set_cf((leftData >> (TypeTrivia<T>::bits - steps)) & 1);
    }

    set_of(get_cf() ^ (result >> (TypeTrivia<T>::bits - 1) & 1));
    update_flags<T>(result);
    return result;
}

void CPU::_SHLD_RM16_reg16_imm8(Instruction& insn)
{
    insn.modrm().write16(doSHLD(insn.modrm().read16(), insn.reg16(), insn.imm8()));
}

void CPU::_SHLD_RM32_reg32_imm8(Instruction& insn)
{
    insn.modrm().write32(doSHLD(insn.modrm().read32(), insn.reg32(), insn.imm8()));
}

void CPU::_SHLD_RM16_reg16_CL(Instruction& insn)
{
    insn.modrm().write16(doSHLD(insn.modrm().read16(), insn.reg16(), get_cl()));
}

void CPU::_SHLD_RM32_reg32_CL(Instruction& insn)
{
    insn.modrm().write32(doSHLD(insn.modrm().read32(), insn.reg32(), get_cl()));
}

template<typename T>
T CPU::doSHRD(T leftData, T rightData, unsigned steps)
{
    steps &= 31;
    if (!steps)
        return rightData;

    T result;
    if (steps > TypeTrivia<T>::bits) {
        result = (rightData << (32 - steps)) | (leftData >> (steps - TypeTrivia<T>::bits));
        set_cf((leftData >> (steps - (TypeTrivia<T>::bits + 1))) & 1);
    } else {
        result = (rightData >> steps) | (leftData << (TypeTrivia<T>::bits - steps));
        set_cf((rightData >> (steps - 1)) & 1);
    }

    set_of((result ^ rightData) >> (TypeTrivia<T>::bits - 1) & 1);
    update_flags<T>(result);
    return result;
}

void CPU::_SHRD_RM16_reg16_imm8(Instruction& insn)
{
    insn.modrm().write16(doSHRD(insn.reg16(), insn.modrm().read16(), insn.imm8()));
}

void CPU::_SHRD_RM32_reg32_imm8(Instruction& insn)
{
    insn.modrm().write32(doSHRD(insn.reg32(), insn.modrm().read32(), insn.imm8()));
}

void CPU::_SHRD_RM16_reg16_CL(Instruction& insn)
{
    insn.modrm().write16(doSHRD(insn.reg16(), insn.modrm().read16(), get_cl()));
}

void CPU::_SHRD_RM32_reg32_CL(Instruction& insn)
{
    insn.modrm().write32(doSHRD(insn.reg32(), insn.modrm().read32(), get_cl()));
}
