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

void CPU::_JCXZ_imm8(Instruction& insn)
{
    if (read_register_for_address_size(RegisterCX) == 0)
        jump_relative8(insn.imm8());
}

void CPU::_JMP_imm16(Instruction& insn)
{
    jump_relative16(insn.imm16());
}

void CPU::_JMP_imm32(Instruction& insn)
{
    jump_relative32(insn.imm32());
}

void CPU::_JMP_imm16_imm16(Instruction& insn)
{
    far_jump(insn.immAddress16_16(), JumpType::JMP);
}

void CPU::_JMP_imm16_imm32(Instruction& insn)
{
    far_jump(insn.immAddress16_32(), JumpType::JMP);
}

void CPU::_JMP_short_imm8(Instruction& insn)
{
    jump_relative8(insn.imm8());
}

void CPU::_JMP_RM16(Instruction& insn)
{
    jump_absolute16(insn.modrm().read16());
}

void CPU::_JMP_RM32(Instruction& insn)
{
    jump_absolute32(insn.modrm().read32());
}

template<typename T>
void CPU::doFarJump(Instruction& insn, JumpType jumpType)
{
    if (insn.modrm().is_register())
        throw InvalidOpcode("Far JMP/CALL with register operand");

    auto address = read_logical_address<T>(insn.modrm().segment(), insn.modrm().offset());
    far_jump(address, jumpType);
}

void CPU::_JMP_FAR_mem16(Instruction& insn)
{
    doFarJump<u16>(insn, JumpType::JMP);
}

void CPU::_JMP_FAR_mem32(Instruction& insn)
{
    doFarJump<u32>(insn, JumpType::JMP);
}

void CPU::_CALL_FAR_mem16(Instruction& insn)
{
    doFarJump<u16>(insn, JumpType::CALL);
}

void CPU::_CALL_FAR_mem32(Instruction& insn)
{
    doFarJump<u32>(insn, JumpType::CALL);
}

void CPU::_CMOVcc_reg16_RM16(Instruction& insn)
{
    if (evaluate(insn.cc()))
        insn.reg16() = insn.modrm().read16();
}

void CPU::_CMOVcc_reg32_RM32(Instruction& insn)
{
    if (evaluate(insn.cc()))
        insn.reg32() = insn.modrm().read32();
}

void CPU::_SETcc_RM8(Instruction& insn)
{
    insn.modrm().write8(evaluate(insn.cc()));
}

void CPU::_Jcc_imm8(Instruction& insn)
{
    if (evaluate(insn.cc()))
        jump_relative8(insn.imm8());
}

void CPU::_Jcc_NEAR_imm(Instruction& insn)
{
    if (!evaluate(insn.cc()))
        return;
    jump_relative32(insn.immAddress());
}

void CPU::_CALL_imm16(Instruction& insn)
{
    push16(get_ip());
    jump_relative16(insn.imm16());
}

void CPU::_CALL_imm32(Instruction& insn)
{
    push32(get_eip());
    jump_relative32(insn.imm32());
}

void CPU::_CALL_imm16_imm16(Instruction& insn)
{
    far_jump(insn.immAddress16_16(), JumpType::CALL);
}

void CPU::_CALL_imm16_imm32(Instruction& insn)
{
    far_jump(insn.immAddress16_32(), JumpType::CALL);
}

void CPU::_CALL_RM16(Instruction& insn)
{
    push16(get_ip());
    jump_absolute16(insn.modrm().read16());
}

void CPU::_CALL_RM32(Instruction& insn)
{
    push32(get_eip());
    jump_absolute32(insn.modrm().read32());
}

void CPU::_RET(Instruction&)
{
    jump_absolute32(pop_operand_sized_value());
}

void CPU::_RET_imm16(Instruction& insn)
{
    jump_absolute32(pop_operand_sized_value());
    adjust_stack_pointer(insn.imm16());
}

void CPU::_RETF(Instruction&)
{
    far_return();
}

void CPU::_RETF_imm16(Instruction& insn)
{
    far_return(insn.imm16());
}

void CPU::doLOOP(Instruction& insn, bool condition)
{
    if (!decrement_cx_for_address_size() && condition)
        jump_relative8(static_cast<i8>(insn.imm8()));
}

void CPU::_LOOP_imm8(Instruction& insn)
{
    doLOOP(insn, true);
}

void CPU::_LOOPZ_imm8(Instruction& insn)
{
    doLOOP(insn, get_zf());
}

void CPU::_LOOPNZ_imm8(Instruction& insn)
{
    doLOOP(insn, !get_zf());
}
