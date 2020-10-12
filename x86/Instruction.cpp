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

#include "Instruction.h"
#include "CPU.h"

enum IsLockPrefixAllowed { LockPrefixNotAllowed = 0,
    LockPrefixAllowed };

enum InstructionFormat {
    InvalidFormat,
    MultibyteWithSlash,
    MultibyteWithSubopcode,
    InstructionPrefix,

    __BeginFormatsWithRMByte,
    OP_RM16_reg16,
    OP_reg8_RM8,
    OP_reg16_RM16,
    OP_RM16_seg,
    OP_RM32_seg,
    OP_RM8_imm8,
    OP_RM16_imm16,
    OP_RM16_imm8,
    OP_RM32_imm8,
    OP_RM8,
    OP_RM16,
    OP_RM32,
    OP_RM8_reg8,
    OP_RM32_reg32,
    OP_reg32_RM32,
    OP_RM32_imm32,
    OP_reg16_RM16_imm8,
    OP_reg32_RM32_imm8,
    OP_reg16_RM16_imm16,
    OP_reg32_RM32_imm32,
    OP_reg16_mem16,
    OP_reg32_mem32,
    OP_seg_RM16,
    OP_seg_RM32,
    OP_RM8_1,
    OP_RM16_1,
    OP_RM32_1,
    OP_FAR_mem16,
    OP_FAR_mem32,
    OP_RM8_CL,
    OP_RM16_CL,
    OP_RM32_CL,
    OP_reg32_CR,
    OP_CR_reg32,
    OP_reg32_DR,
    OP_DR_reg32,
    OP_reg16_RM8,
    OP_reg32_RM8,
    OP_reg32_RM16,
    OP_RM16_reg16_imm8,
    OP_RM32_reg32_imm8,
    OP_RM16_reg16_CL,
    OP_RM32_reg32_CL,
    __EndFormatsWithRMByte,

    OP_reg32_imm32,
    OP_AL_imm8,
    OP_AX_imm16,
    OP_EAX_imm32,
    OP_CS,
    OP_DS,
    OP_ES,
    OP_SS,
    OP_FS,
    OP_GS,
    OP,
    OP_reg16,
    OP_imm16,
    OP_relimm16,
    OP_relimm32,
    OP_imm8,
    OP_imm16_imm16,
    OP_imm16_imm32,
    OP_AX_reg16,
    OP_EAX_reg32,
    OP_AL_moff8,
    OP_AX_moff16,
    OP_EAX_moff32,
    OP_moff8_AL,
    OP_moff16_AX,
    OP_moff32_EAX,
    OP_reg8_imm8,
    OP_reg16_imm16,
    OP_3,
    OP_AX_imm8,
    OP_EAX_imm8,
    OP_short_imm8,
    OP_AL_DX,
    OP_AX_DX,
    OP_EAX_DX,
    OP_DX_AL,
    OP_DX_AX,
    OP_DX_EAX,
    OP_imm8_AL,
    OP_imm8_AX,
    OP_imm8_EAX,
    OP_reg8_CL,

    OP_reg32,
    OP_imm32,
    OP_imm8_imm16,

    OP_NEAR_imm,
};

static const unsigned CurrentAddressSize = 0xB33FBABE;

struct InstructionDescriptor {
    InstructionImpl impl { nullptr };
    bool opcode_has_register_index { false };
    const char* mnemonic { nullptr };
    InstructionFormat format { InvalidFormat };
    bool has_rm { false };
    unsigned imm1_bytes { 0 };
    unsigned imm2_bytes { 0 };
    InstructionDescriptor* slashes { nullptr };

    unsigned imm1_bytes_for_address_size(bool a32)
    {
        if (imm1_bytes == CurrentAddressSize)
            return a32 ? 4 : 2;
        return imm1_bytes;
    }

    unsigned imm2_bytes_for_address_size(bool a32)
    {
        if (imm2_bytes == CurrentAddressSize)
            return a32 ? 4 : 2;
        return imm2_bytes;
    }

    IsLockPrefixAllowed lock_prefix_allowed { LockPrefixNotAllowed };
};

static InstructionDescriptor s_table16[256];
static InstructionDescriptor s_table32[256];
static InstructionDescriptor s_0f_table16[256];
static InstructionDescriptor s_0f_table32[256];

static bool opcode_has_register_index(u8 op)
{
    if (op >= 0x40 && op <= 0x5F)
        return true;
    if (op >= 0x90 && op <= 0x97)
        return true;
    if (op >= 0xB0 && op <= 0xBF)
        return true;
    return false;
}

static void build(InstructionDescriptor* table, u8 op, const char* mnemonic, InstructionFormat format, InstructionImpl impl, IsLockPrefixAllowed lock_prefix_allowed)
{
    InstructionDescriptor& d = table[op];
    ASSERT(!d.impl);

    d.mnemonic = mnemonic;
    d.format = format;
    d.impl = impl;
    d.lock_prefix_allowed = lock_prefix_allowed;

    if ((format > __BeginFormatsWithRMByte && format < __EndFormatsWithRMByte) || format == MultibyteWithSlash)
        d.has_rm = true;
    else
        d.opcode_has_register_index = opcode_has_register_index(op);

    switch (format) {
    case OP_RM8_imm8:
    case OP_RM16_imm8:
    case OP_RM32_imm8:
    case OP_reg16_RM16_imm8:
    case OP_reg32_RM32_imm8:
    case OP_AL_imm8:
    case OP_imm8:
    case OP_reg8_imm8:
    case OP_AX_imm8:
    case OP_EAX_imm8:
    case OP_short_imm8:
    case OP_imm8_AL:
    case OP_imm8_AX:
    case OP_imm8_EAX:
    case OP_RM16_reg16_imm8:
    case OP_RM32_reg32_imm8:
        d.imm1_bytes = 1;
        break;
    case OP_reg16_RM16_imm16:
    case OP_AX_imm16:
    case OP_imm16:
    case OP_relimm16:
    case OP_reg16_imm16:
    case OP_RM16_imm16:
        d.imm1_bytes = 2;
        break;
    case OP_RM32_imm32:
    case OP_reg32_RM32_imm32:
    case OP_reg32_imm32:
    case OP_EAX_imm32:
    case OP_imm32:
    case OP_relimm32:
        d.imm1_bytes = 4;
        break;
    case OP_imm8_imm16:
        d.imm1_bytes = 1;
        d.imm2_bytes = 2;
        break;
    case OP_imm16_imm16:
        d.imm1_bytes = 2;
        d.imm2_bytes = 2;
        break;
    case OP_imm16_imm32:
        d.imm1_bytes = 2;
        d.imm2_bytes = 4;
        break;
    case OP_moff8_AL:
    case OP_moff16_AX:
    case OP_moff32_EAX:
    case OP_AL_moff8:
    case OP_AX_moff16:
    case OP_EAX_moff32:
    case OP_NEAR_imm:
        d.imm1_bytes = CurrentAddressSize;
        break;
    //default:
    case InvalidFormat:
    case MultibyteWithSlash:
    case MultibyteWithSubopcode:
    case InstructionPrefix:
    case __BeginFormatsWithRMByte:
    case OP_RM16_reg16:
    case OP_reg8_RM8:
    case OP_reg16_RM16:
    case OP_RM16_seg:
    case OP_RM32_seg:
    case OP_RM8:
    case OP_RM16:
    case OP_RM32:
    case OP_RM8_reg8:
    case OP_RM32_reg32:
    case OP_reg32_RM32:
    case OP_reg16_mem16:
    case OP_reg32_mem32:
    case OP_seg_RM16:
    case OP_seg_RM32:
    case OP_RM8_1:
    case OP_RM16_1:
    case OP_RM32_1:
    case OP_FAR_mem16:
    case OP_FAR_mem32:
    case OP_RM8_CL:
    case OP_RM16_CL:
    case OP_RM32_CL:
    case OP_reg32_CR:
    case OP_CR_reg32:
    case OP_reg16_RM8:
    case OP_reg32_RM8:
    case __EndFormatsWithRMByte:
    case OP_CS:
    case OP_DS:
    case OP_ES:
    case OP_SS:
    case OP_FS:
    case OP_GS:
    case OP:
    case OP_reg16:
    case OP_AX_reg16:
    case OP_EAX_reg32:
    case OP_3:
    case OP_AL_DX:
    case OP_AX_DX:
    case OP_EAX_DX:
    case OP_DX_AL:
    case OP_DX_AX:
    case OP_DX_EAX:
    case OP_reg8_CL:
    case OP_reg32:
    case OP_reg32_RM16:
    case OP_reg32_DR:
    case OP_DR_reg32:
    case OP_RM16_reg16_CL:
    case OP_RM32_reg32_CL:
        break;
    }
}

static void build_slash(InstructionDescriptor* table, u8 op, u8 slash, const char* mnemonic, InstructionFormat format, InstructionImpl impl, IsLockPrefixAllowed lock_prefix_allowed = LockPrefixNotAllowed)
{
    InstructionDescriptor& d = table[op];
    d.format = MultibyteWithSlash;
    d.has_rm = true;
    if (!d.slashes)
        d.slashes = new InstructionDescriptor[8];

    build(d.slashes, slash, mnemonic, format, impl, lock_prefix_allowed);
}

static void build_0f(u8 op, const char* mnemonic, InstructionFormat format, void (CPU::*impl)(Instruction&), IsLockPrefixAllowed lock_prefix_allowed = LockPrefixNotAllowed)
{
    build(s_0f_table16, op, mnemonic, format, impl, lock_prefix_allowed);
    build(s_0f_table32, op, mnemonic, format, impl, lock_prefix_allowed);
}

static void build(u8 op, const char* mnemonic, InstructionFormat format, void (CPU::*impl)(Instruction&), IsLockPrefixAllowed lock_prefix_allowed = LockPrefixNotAllowed)
{
    build(s_table16, op, mnemonic, format, impl, lock_prefix_allowed);
    build(s_table32, op, mnemonic, format, impl, lock_prefix_allowed);
}

static void build(u8 op, const char* mnemonic, InstructionFormat format16, void (CPU::*impl16)(Instruction&), InstructionFormat format32, void (CPU::*impl32)(Instruction&), IsLockPrefixAllowed lock_prefix_allowed = LockPrefixNotAllowed)
{
    build(s_table16, op, mnemonic, format16, impl16, lock_prefix_allowed);
    build(s_table32, op, mnemonic, format32, impl32, lock_prefix_allowed);
}

static void build_0f(u8 op, const char* mnemonic, InstructionFormat format16, void (CPU::*impl16)(Instruction&), InstructionFormat format32, void (CPU::*impl32)(Instruction&), IsLockPrefixAllowed lock_prefix_allowed = LockPrefixNotAllowed)
{
    build(s_0f_table16, op, mnemonic, format16, impl16, lock_prefix_allowed);
    build(s_0f_table32, op, mnemonic, format32, impl32, lock_prefix_allowed);
}

static void build(u8 op, const char* mnemonic16, InstructionFormat format16, void (CPU::*impl16)(Instruction&), const char* mnemonic32, InstructionFormat format32, void (CPU::*impl32)(Instruction&), IsLockPrefixAllowed lock_prefix_allowed = LockPrefixNotAllowed)
{
    build(s_table16, op, mnemonic16, format16, impl16, lock_prefix_allowed);
    build(s_table32, op, mnemonic32, format32, impl32, lock_prefix_allowed);
}

static void build_0f(u8 op, const char* mnemonic16, InstructionFormat format16, void (CPU::*impl16)(Instruction&), const char* mnemonic32, InstructionFormat format32, void (CPU::*impl32)(Instruction&), IsLockPrefixAllowed lock_prefix_allowed = LockPrefixNotAllowed)
{
    build(s_0f_table16, op, mnemonic16, format16, impl16, lock_prefix_allowed);
    build(s_0f_table32, op, mnemonic32, format32, impl32, lock_prefix_allowed);
}

static void build_slash(u8 op, u8 slash, const char* mnemonic, InstructionFormat format, void (CPU::*impl)(Instruction&), IsLockPrefixAllowed lock_prefix_allowed = LockPrefixNotAllowed)
{
    build_slash(s_table16, op, slash, mnemonic, format, impl, lock_prefix_allowed);
    build_slash(s_table32, op, slash, mnemonic, format, impl, lock_prefix_allowed);
}

static void build_slash(u8 op, u8 slash, const char* mnemonic, InstructionFormat format16, void (CPU::*impl16)(Instruction&), InstructionFormat format32, void (CPU::*impl32)(Instruction&), IsLockPrefixAllowed lock_prefix_allowed = LockPrefixNotAllowed)
{
    build_slash(s_table16, op, slash, mnemonic, format16, impl16, lock_prefix_allowed);
    build_slash(s_table32, op, slash, mnemonic, format32, impl32, lock_prefix_allowed);
}

static void build_0f_slash(u8 op, u8 slash, const char* mnemonic, InstructionFormat format16, void (CPU::*impl16)(Instruction&), InstructionFormat format32, void (CPU::*impl32)(Instruction&), IsLockPrefixAllowed lock_prefix_allowed = LockPrefixNotAllowed)
{
    build_slash(s_0f_table16, op, slash, mnemonic, format16, impl16, lock_prefix_allowed);
    build_slash(s_0f_table32, op, slash, mnemonic, format32, impl32, lock_prefix_allowed);
}

static void build_0f_slash(u8 op, u8 slash, const char* mnemonic, InstructionFormat format, void (CPU::*impl)(Instruction&), IsLockPrefixAllowed lock_prefix_allowed = LockPrefixNotAllowed)
{
    build_slash(s_0f_table16, op, slash, mnemonic, format, impl, lock_prefix_allowed);
    build_slash(s_0f_table32, op, slash, mnemonic, format, impl, lock_prefix_allowed);
}

void build_opcode_tables_if_needed()
{
    static bool has_built_tables = false;
    if (has_built_tables)
        return;

    build(0x00, "ADD", OP_RM8_reg8, &CPU::_ADD_RM8_reg8, LockPrefixAllowed);
    build(0x01, "ADD", OP_RM16_reg16, &CPU::_ADD_RM16_reg16, OP_RM32_reg32, &CPU::_ADD_RM32_reg32, LockPrefixAllowed);
    build(0x02, "ADD", OP_reg8_RM8, &CPU::_ADD_reg8_RM8, LockPrefixAllowed);
    build(0x03, "ADD", OP_reg16_RM16, &CPU::_ADD_reg16_RM16, OP_reg32_RM32, &CPU::_ADD_reg32_RM32, LockPrefixAllowed);
    build(0x04, "ADD", OP_AL_imm8, &CPU::_ADD_AL_imm8);
    build(0x05, "ADD", OP_AX_imm16, &CPU::_ADD_AX_imm16, OP_EAX_imm32, &CPU::_ADD_EAX_imm32);
    build(0x06, "PUSH", OP_ES, &CPU::_PUSH_ES);
    build(0x07, "POP", OP_ES, &CPU::_POP_ES);
    build(0x08, "OR", OP_RM8_reg8, &CPU::_OR_RM8_reg8, LockPrefixAllowed);
    build(0x09, "OR", OP_RM16_reg16, &CPU::_OR_RM16_reg16, OP_RM32_reg32, &CPU::_OR_RM32_reg32, LockPrefixAllowed);
    build(0x0A, "OR", OP_reg8_RM8, &CPU::_OR_reg8_RM8, LockPrefixAllowed);
    build(0x0B, "OR", OP_reg16_RM16, &CPU::_OR_reg16_RM16, OP_reg32_RM32, &CPU::_OR_reg32_RM32, LockPrefixAllowed);
    build(0x0C, "OR", OP_AL_imm8, &CPU::_OR_AL_imm8);
    build(0x0D, "OR", OP_AX_imm16, &CPU::_OR_AX_imm16, OP_EAX_imm32, &CPU::_OR_EAX_imm32);
    build(0x0E, "PUSH", OP_CS, &CPU::_PUSH_CS);

    build(0x10, "ADC", OP_RM8_reg8, &CPU::_ADC_RM8_reg8, LockPrefixAllowed);
    build(0x11, "ADC", OP_RM16_reg16, &CPU::_ADC_RM16_reg16, OP_RM32_reg32, &CPU::_ADC_RM32_reg32, LockPrefixAllowed);
    build(0x12, "ADC", OP_reg8_RM8, &CPU::_ADC_reg8_RM8, LockPrefixAllowed);
    build(0x13, "ADC", OP_reg16_RM16, &CPU::_ADC_reg16_RM16, OP_reg32_RM32, &CPU::_ADC_reg32_RM32, LockPrefixAllowed);
    build(0x14, "ADC", OP_AL_imm8, &CPU::_ADC_AL_imm8);
    build(0x15, "ADC", OP_AX_imm16, &CPU::_ADC_AX_imm16, OP_EAX_imm32, &CPU::_ADC_EAX_imm32);
    build(0x16, "PUSH", OP_SS, &CPU::_PUSH_SS);
    build(0x17, "POP", OP_SS, &CPU::_POP_SS);
    build(0x18, "SBB", OP_RM8_reg8, &CPU::_SBB_RM8_reg8, LockPrefixAllowed);
    build(0x19, "SBB", OP_RM16_reg16, &CPU::_SBB_RM16_reg16, OP_RM32_reg32, &CPU::_SBB_RM32_reg32, LockPrefixAllowed);
    build(0x1A, "SBB", OP_reg8_RM8, &CPU::_SBB_reg8_RM8, LockPrefixAllowed);
    build(0x1B, "SBB", OP_reg16_RM16, &CPU::_SBB_reg16_RM16, OP_reg32_RM32, &CPU::_SBB_reg32_RM32, LockPrefixAllowed);
    build(0x1C, "SBB", OP_AL_imm8, &CPU::_SBB_AL_imm8);
    build(0x1D, "SBB", OP_AX_imm16, &CPU::_SBB_AX_imm16, OP_EAX_imm32, &CPU::_SBB_EAX_imm32);
    build(0x1E, "PUSH", OP_DS, &CPU::_PUSH_DS);
    build(0x1F, "POP", OP_DS, &CPU::_POP_DS);

    build(0x20, "AND", OP_RM8_reg8, &CPU::_AND_RM8_reg8, LockPrefixAllowed);
    build(0x21, "AND", OP_RM16_reg16, &CPU::_AND_RM16_reg16, OP_RM32_reg32, &CPU::_AND_RM32_reg32, LockPrefixAllowed);
    build(0x22, "AND", OP_reg8_RM8, &CPU::_AND_reg8_RM8, LockPrefixAllowed);
    build(0x23, "AND", OP_reg16_RM16, &CPU::_AND_reg16_RM16, OP_reg32_RM32, &CPU::_AND_reg32_RM32, LockPrefixAllowed);
    build(0x24, "AND", OP_AL_imm8, &CPU::_AND_AL_imm8);
    build(0x25, "AND", OP_AX_imm16, &CPU::_AND_AX_imm16, OP_EAX_imm32, &CPU::_AND_EAX_imm32);
    build(0x27, "DAA", OP, &CPU::_DAA);
    build(0x28, "SUB", OP_RM8_reg8, &CPU::_SUB_RM8_reg8, LockPrefixAllowed);
    build(0x29, "SUB", OP_RM16_reg16, &CPU::_SUB_RM16_reg16, OP_RM32_reg32, &CPU::_SUB_RM32_reg32, LockPrefixAllowed);
    build(0x2A, "SUB", OP_reg8_RM8, &CPU::_SUB_reg8_RM8, LockPrefixAllowed);
    build(0x2B, "SUB", OP_reg16_RM16, &CPU::_SUB_reg16_RM16, OP_reg32_RM32, &CPU::_SUB_reg32_RM32, LockPrefixAllowed);
    build(0x2C, "SUB", OP_AL_imm8, &CPU::_SUB_AL_imm8);
    build(0x2D, "SUB", OP_AX_imm16, &CPU::_SUB_AX_imm16, OP_EAX_imm32, &CPU::_SUB_EAX_imm32);
    build(0x2F, "DAS", OP, &CPU::_DAS);

    build(0x30, "XOR", OP_RM8_reg8, &CPU::_XOR_RM8_reg8, LockPrefixAllowed);
    build(0x31, "XOR", OP_RM16_reg16, &CPU::_XOR_RM16_reg16, OP_RM32_reg32, &CPU::_XOR_RM32_reg32, LockPrefixAllowed);
    build(0x32, "XOR", OP_reg8_RM8, &CPU::_XOR_reg8_RM8, LockPrefixAllowed);
    build(0x33, "XOR", OP_reg16_RM16, &CPU::_XOR_reg16_RM16, OP_reg32_RM32, &CPU::_XOR_reg32_RM32, LockPrefixAllowed);
    build(0x34, "XOR", OP_AL_imm8, &CPU::_XOR_AL_imm8);
    build(0x35, "XOR", OP_AX_imm16, &CPU::_XOR_AX_imm16, OP_EAX_imm32, &CPU::_XOR_EAX_imm32);
    build(0x37, "AAA", OP, &CPU::_AAA);
    build(0x38, "CMP", OP_RM8_reg8, &CPU::_CMP_RM8_reg8, LockPrefixAllowed);
    build(0x39, "CMP", OP_RM16_reg16, &CPU::_CMP_RM16_reg16, OP_RM32_reg32, &CPU::_CMP_RM32_reg32, LockPrefixAllowed);
    build(0x3A, "CMP", OP_reg8_RM8, &CPU::_CMP_reg8_RM8, LockPrefixAllowed);
    build(0x3B, "CMP", OP_reg16_RM16, &CPU::_CMP_reg16_RM16, OP_reg32_RM32, &CPU::_CMP_reg32_RM32, LockPrefixAllowed);
    build(0x3C, "CMP", OP_AL_imm8, &CPU::_CMP_AL_imm8);
    build(0x3D, "CMP", OP_AX_imm16, &CPU::_CMP_AX_imm16, OP_EAX_imm32, &CPU::_CMP_EAX_imm32);
    build(0x3F, "AAS", OP, &CPU::_AAS);

    for (u8 i = 0; i <= 7; ++i)
        build(0x40 + i, "INC", OP_reg16, &CPU::_INC_reg16, OP_reg32, &CPU::_INC_reg32);

    for (u8 i = 0; i <= 7; ++i)
        build(0x48 + i, "DEC", OP_reg16, &CPU::_DEC_reg16, OP_reg32, &CPU::_DEC_reg32);

    for (u8 i = 0; i <= 7; ++i)
        build(0x50 + i, "PUSH", OP_reg16, &CPU::_PUSH_reg16, OP_reg32, &CPU::_PUSH_reg32);

    for (u8 i = 0; i <= 7; ++i)
        build(0x58 + i, "POP", OP_reg16, &CPU::_POP_reg16, OP_reg32, &CPU::_POP_reg32);

    build(0x60, "PUSHAW", OP, &CPU::_PUSHA, "PUSHAD", OP, &CPU::_PUSHAD);
    build(0x61, "POPAW", OP, &CPU::_POPA, "POPAD", OP, &CPU::_POPAD);
    build(0x62, "BOUND", OP_reg16_RM16, &CPU::_BOUND, "BOUND", OP_reg32_RM32, &CPU::_BOUND);
    build(0x63, "ARPL", OP_RM16_reg16, &CPU::_ARPL);

    build(0x68, "PUSH", OP_imm16, &CPU::_PUSH_imm16, OP_imm32, &CPU::_PUSH_imm32);
    build(0x69, "IMUL", OP_reg16_RM16_imm16, &CPU::_IMUL_reg16_RM16_imm16, OP_reg32_RM32_imm32, &CPU::_IMUL_reg32_RM32_imm32);
    build(0x6A, "PUSH", OP_imm8, &CPU::_PUSH_imm8);
    build(0x6B, "IMUL", OP_reg16_RM16_imm8, &CPU::_IMUL_reg16_RM16_imm8, OP_reg32_RM32_imm8, &CPU::_IMUL_reg32_RM32_imm8);
    build(0x6C, "INSB", OP, &CPU::_INSB);
    build(0x6D, "INSW", OP, &CPU::_INSW, "INSD", OP, &CPU::_INSD);
    build(0x6E, "OUTSB", OP, &CPU::_OUTSB);
    build(0x6F, "OUTSW", OP, &CPU::_OUTSW, "OUTSD", OP, &CPU::_OUTSD);

    build(0x70, "JO", OP_short_imm8, &CPU::_Jcc_imm8);
    build(0x71, "JNO", OP_short_imm8, &CPU::_Jcc_imm8);
    build(0x72, "JC", OP_short_imm8, &CPU::_Jcc_imm8);
    build(0x73, "JNC", OP_short_imm8, &CPU::_Jcc_imm8);
    build(0x74, "JZ", OP_short_imm8, &CPU::_Jcc_imm8);
    build(0x75, "JNZ", OP_short_imm8, &CPU::_Jcc_imm8);
    build(0x76, "JNA", OP_short_imm8, &CPU::_Jcc_imm8);
    build(0x77, "JA", OP_short_imm8, &CPU::_Jcc_imm8);
    build(0x78, "JS", OP_short_imm8, &CPU::_Jcc_imm8);
    build(0x79, "JNS", OP_short_imm8, &CPU::_Jcc_imm8);
    build(0x7A, "JP", OP_short_imm8, &CPU::_Jcc_imm8);
    build(0x7B, "JNP", OP_short_imm8, &CPU::_Jcc_imm8);
    build(0x7C, "JL", OP_short_imm8, &CPU::_Jcc_imm8);
    build(0x7D, "JNL", OP_short_imm8, &CPU::_Jcc_imm8);
    build(0x7E, "JNG", OP_short_imm8, &CPU::_Jcc_imm8);
    build(0x7F, "JG", OP_short_imm8, &CPU::_Jcc_imm8);

    build(0x84, "TEST", OP_RM8_reg8, &CPU::_TEST_RM8_reg8);
    build(0x85, "TEST", OP_RM16_reg16, &CPU::_TEST_RM16_reg16, OP_RM32_reg32, &CPU::_TEST_RM32_reg32);
    build(0x86, "XCHG", OP_reg8_RM8, &CPU::_XCHG_reg8_RM8, LockPrefixAllowed);
    build(0x87, "XCHG", OP_reg16_RM16, &CPU::_XCHG_reg16_RM16, OP_reg32_RM32, &CPU::_XCHG_reg32_RM32, LockPrefixAllowed);
    build(0x88, "MOV", OP_RM8_reg8, &CPU::_MOV_RM8_reg8);
    build(0x89, "MOV", OP_RM16_reg16, &CPU::_MOV_RM16_reg16, OP_RM32_reg32, &CPU::_MOV_RM32_reg32);
    build(0x8A, "MOV", OP_reg8_RM8, &CPU::_MOV_reg8_RM8);
    build(0x8B, "MOV", OP_reg16_RM16, &CPU::_MOV_reg16_RM16, OP_reg32_RM32, &CPU::_MOV_reg32_RM32);
    build(0x8C, "MOV", OP_RM16_seg, &CPU::_MOV_RM16_seg);
    build(0x8D, "LEA", OP_reg16_mem16, &CPU::_LEA_reg16_mem16, OP_reg32_mem32, &CPU::_LEA_reg32_mem32);
    build(0x8E, "MOV", OP_seg_RM16, &CPU::_MOV_seg_RM16, OP_seg_RM32, &CPU::_MOV_seg_RM32);

    build(0x90, "NOP", OP, &CPU::_NOP);

    for (u8 i = 0; i <= 6; ++i)
        build(0x91 + i, "XCHG", OP_AX_reg16, &CPU::_XCHG_AX_reg16, OP_EAX_reg32, &CPU::_XCHG_EAX_reg32);

    build(0x98, "CBW", OP, &CPU::_CBW, "CWDE", OP, &CPU::_CWDE);
    build(0x99, "CWD", OP, &CPU::_CWD, "CDQ", OP, &CPU::_CDQ);
    build(0x9A, "CALL", OP_imm16_imm16, &CPU::_CALL_imm16_imm16, OP_imm16_imm32, &CPU::_CALL_imm16_imm32);
    build(0x9B, "WAIT", OP, &CPU::_WAIT);
    build(0x9C, "PUSHFW", OP, &CPU::_PUSHF, "PUSHFD", OP, &CPU::_PUSHFD);
    build(0x9D, "POPFW", OP, &CPU::_POPF, "POPFD", OP, &CPU::_POPFD);
    build(0x9E, "SAHF", OP, &CPU::_SAHF);
    build(0x9F, "LAHF", OP, &CPU::_LAHF);

    build(0xA0, "MOV", OP_AL_moff8, &CPU::_MOV_AL_moff8);
    build(0xA1, "MOV", OP_AX_moff16, &CPU::_MOV_AX_moff16, OP_EAX_moff32, &CPU::_MOV_EAX_moff32);
    build(0xA2, "MOV", OP_moff8_AL, &CPU::_MOV_moff8_AL);
    build(0xA3, "MOV", OP_moff16_AX, &CPU::_MOV_moff16_AX, OP_moff32_EAX, &CPU::_MOV_moff32_EAX);
    build(0xA4, "MOVSB", OP, &CPU::_MOVSB);
    build(0xA5, "MOVSW", OP, &CPU::_MOVSW, "MOVSD", OP, &CPU::_MOVSD);
    build(0xA6, "CMPSB", OP, &CPU::_CMPSB);
    build(0xA7, "CMPSW", OP, &CPU::_CMPSW, "CMPSD", OP, &CPU::_CMPSD);
    build(0xA8, "TEST", OP_AL_imm8, &CPU::_TEST_AL_imm8);
    build(0xA9, "TEST", OP_AX_imm16, &CPU::_TEST_AX_imm16, OP_EAX_imm32, &CPU::_TEST_EAX_imm32);
    build(0xAA, "STOSB", OP, &CPU::_STOSB);
    build(0xAB, "STOSW", OP, &CPU::_STOSW, "STOSD", OP, &CPU::_STOSD);
    build(0xAC, "LODSB", OP, &CPU::_LODSB);
    build(0xAD, "LODSW", OP, &CPU::_LODSW, "LODSD", OP, &CPU::_LODSD);
    build(0xAE, "SCASB", OP, &CPU::_SCASB);
    build(0xAF, "SCASW", OP, &CPU::_SCASW, "SCASD", OP, &CPU::_SCASD);

    for (u8 i = 0xb0; i <= 0xb7; ++i)
        build(i, "MOV", OP_reg8_imm8, &CPU::_MOV_reg8_imm8);

    for (u8 i = 0xb8; i <= 0xbf; ++i)
        build(i, "MOV", OP_reg16_imm16, &CPU::_MOV_reg16_imm16, OP_reg32_imm32, &CPU::_MOV_reg32_imm32);

    build(0xC2, "RET", OP_imm16, &CPU::_RET_imm16);
    build(0xC3, "RET", OP, &CPU::_RET);
    build(0xC4, "LES", OP_reg16_mem16, &CPU::_LES_reg16_mem16, OP_reg32_mem32, &CPU::_LES_reg32_mem32);
    build(0xC5, "LDS", OP_reg16_mem16, &CPU::_LDS_reg16_mem16, OP_reg32_mem32, &CPU::_LDS_reg32_mem32);
    build(0xC6, "MOV", OP_RM8_imm8, &CPU::_MOV_RM8_imm8);
    build(0xC7, "MOV", OP_RM16_imm16, &CPU::_MOV_RM16_imm16, OP_RM32_imm32, &CPU::_MOV_RM32_imm32);
    build(0xC8, "ENTER", OP_imm8_imm16, &CPU::_ENTER16, OP_imm8_imm16, &CPU::_ENTER32);
    build(0xC9, "LEAVE", OP, &CPU::_LEAVE16, OP, &CPU::_LEAVE32);
    build(0xCA, "RETF", OP_imm16, &CPU::_RETF_imm16);
    build(0xCB, "RETF", OP, &CPU::_RETF);
    build(0xCC, "INT3", OP_3, &CPU::_INT3);
    build(0xCD, "INT", OP_imm8, &CPU::_INT_imm8);
    build(0xCE, "INTO", OP, &CPU::_INTO);
    build(0xCF, "IRET", OP, &CPU::_IRET);

    build(0xD4, "AAM", OP_imm8, &CPU::_AAM);
    build(0xD5, "AAD", OP_imm8, &CPU::_AAD);
    build(0xD6, "SALC", OP, &CPU::_SALC);
    build(0xD7, "XLAT", OP, &CPU::_XLAT);

    // FIXME: D8-DF == FPU
    for (u8 i = 0; i <= 7; ++i)
        build(0xD8 + i, "FPU?", OP_RM8, &CPU::_ESCAPE);

    build(0xE0, "LOOPNZ", OP_imm8, &CPU::_LOOPNZ_imm8);
    build(0xE1, "LOOPZ", OP_imm8, &CPU::_LOOPZ_imm8);
    build(0xE2, "LOOP", OP_imm8, &CPU::_LOOP_imm8);
    build(0xE3, "JCXZ", OP_imm8, &CPU::_JCXZ_imm8);
    build(0xE4, "IN", OP_AL_imm8, &CPU::_IN_AL_imm8);
    build(0xE5, "IN", OP_AX_imm8, &CPU::_IN_AX_imm8, OP_EAX_imm8, &CPU::_IN_EAX_imm8);
    build(0xE6, "OUT", OP_imm8_AL, &CPU::_OUT_imm8_AL);
    build(0xE7, "OUT", OP_imm8_AX, &CPU::_OUT_imm8_AX, OP_imm8_EAX, &CPU::_OUT_imm8_EAX);
    build(0xE8, "CALL", OP_relimm16, &CPU::_CALL_imm16, OP_relimm32, &CPU::_CALL_imm32);
    build(0xE9, "JMP", OP_relimm16, &CPU::_JMP_imm16, OP_relimm32, &CPU::_JMP_imm32);
    build(0xEA, "JMP", OP_imm16_imm16, &CPU::_JMP_imm16_imm16, OP_imm16_imm32, &CPU::_JMP_imm16_imm32);
    build(0xEB, "JMP", OP_short_imm8, &CPU::_JMP_short_imm8);
    build(0xEC, "IN", OP_AL_DX, &CPU::_IN_AL_DX);
    build(0xED, "IN", OP_AX_DX, &CPU::_IN_AX_DX, OP_EAX_DX, &CPU::_IN_EAX_DX);
    build(0xEE, "OUT", OP_DX_AL, &CPU::_OUT_DX_AL);
    build(0xEF, "OUT", OP_DX_AX, &CPU::_OUT_DX_AX, OP_DX_EAX, &CPU::_OUT_DX_EAX);

    build(0xF1, "VKILL", OP, &CPU::_VKILL);

    build(0xF4, "HLT", OP, &CPU::_HLT);
    build(0xF5, "CMC", OP, &CPU::_CMC);

    build(0xF8, "CLC", OP, &CPU::_CLC);
    build(0xF9, "STC", OP, &CPU::_STC);
    build(0xFA, "CLI", OP, &CPU::_CLI);
    build(0xFB, "STI", OP, &CPU::_STI);
    build(0xFC, "CLD", OP, &CPU::_CLD);
    build(0xFD, "STD", OP, &CPU::_STD);

    build_slash(0x80, 0, "ADD", OP_RM8_imm8, &CPU::_ADD_RM8_imm8, LockPrefixAllowed);
    build_slash(0x80, 1, "OR", OP_RM8_imm8, &CPU::_OR_RM8_imm8, LockPrefixAllowed);
    build_slash(0x80, 2, "ADC", OP_RM8_imm8, &CPU::_ADC_RM8_imm8, LockPrefixAllowed);
    build_slash(0x80, 3, "SBB", OP_RM8_imm8, &CPU::_SBB_RM8_imm8, LockPrefixAllowed);
    build_slash(0x80, 4, "AND", OP_RM8_imm8, &CPU::_AND_RM8_imm8, LockPrefixAllowed);
    build_slash(0x80, 5, "SUB", OP_RM8_imm8, &CPU::_SUB_RM8_imm8, LockPrefixAllowed);
    build_slash(0x80, 6, "XOR", OP_RM8_imm8, &CPU::_XOR_RM8_imm8, LockPrefixAllowed);
    build_slash(0x80, 7, "CMP", OP_RM8_imm8, &CPU::_CMP_RM8_imm8);

    build_slash(0x81, 0, "ADD", OP_RM16_imm16, &CPU::_ADD_RM16_imm16, OP_RM32_imm32, &CPU::_ADD_RM32_imm32, LockPrefixAllowed);
    build_slash(0x81, 1, "OR", OP_RM16_imm16, &CPU::_OR_RM16_imm16, OP_RM32_imm32, &CPU::_OR_RM32_imm32, LockPrefixAllowed);
    build_slash(0x81, 2, "ADC", OP_RM16_imm16, &CPU::_ADC_RM16_imm16, OP_RM32_imm32, &CPU::_ADC_RM32_imm32, LockPrefixAllowed);
    build_slash(0x81, 3, "SBB", OP_RM16_imm16, &CPU::_SBB_RM16_imm16, OP_RM32_imm32, &CPU::_SBB_RM32_imm32, LockPrefixAllowed);
    build_slash(0x81, 4, "AND", OP_RM16_imm16, &CPU::_AND_RM16_imm16, OP_RM32_imm32, &CPU::_AND_RM32_imm32, LockPrefixAllowed);
    build_slash(0x81, 5, "SUB", OP_RM16_imm16, &CPU::_SUB_RM16_imm16, OP_RM32_imm32, &CPU::_SUB_RM32_imm32, LockPrefixAllowed);
    build_slash(0x81, 6, "XOR", OP_RM16_imm16, &CPU::_XOR_RM16_imm16, OP_RM32_imm32, &CPU::_XOR_RM32_imm32, LockPrefixAllowed);
    build_slash(0x81, 7, "CMP", OP_RM16_imm16, &CPU::_CMP_RM16_imm16, OP_RM32_imm32, &CPU::_CMP_RM32_imm32);

    build_slash(0x83, 0, "ADD", OP_RM16_imm8, &CPU::_ADD_RM16_imm8, OP_RM32_imm8, &CPU::_ADD_RM32_imm8, LockPrefixAllowed);
    build_slash(0x83, 1, "OR", OP_RM16_imm8, &CPU::_OR_RM16_imm8, OP_RM32_imm8, &CPU::_OR_RM32_imm8, LockPrefixAllowed);
    build_slash(0x83, 2, "ADC", OP_RM16_imm8, &CPU::_ADC_RM16_imm8, OP_RM32_imm8, &CPU::_ADC_RM32_imm8, LockPrefixAllowed);
    build_slash(0x83, 3, "SBB", OP_RM16_imm8, &CPU::_SBB_RM16_imm8, OP_RM32_imm8, &CPU::_SBB_RM32_imm8, LockPrefixAllowed);
    build_slash(0x83, 4, "AND", OP_RM16_imm8, &CPU::_AND_RM16_imm8, OP_RM32_imm8, &CPU::_AND_RM32_imm8, LockPrefixAllowed);
    build_slash(0x83, 5, "SUB", OP_RM16_imm8, &CPU::_SUB_RM16_imm8, OP_RM32_imm8, &CPU::_SUB_RM32_imm8, LockPrefixAllowed);
    build_slash(0x83, 6, "XOR", OP_RM16_imm8, &CPU::_XOR_RM16_imm8, OP_RM32_imm8, &CPU::_XOR_RM32_imm8, LockPrefixAllowed);
    build_slash(0x83, 7, "CMP", OP_RM16_imm8, &CPU::_CMP_RM16_imm8, OP_RM32_imm8, &CPU::_CMP_RM32_imm8);

    build_slash(0x8F, 0, "POP", OP_RM16, &CPU::_POP_RM16, OP_RM32, &CPU::_POP_RM32);

    build_slash(0xC0, 0, "ROL", OP_RM8_imm8, &CPU::_ROL_RM8_imm8);
    build_slash(0xC0, 1, "ROR", OP_RM8_imm8, &CPU::_ROR_RM8_imm8);
    build_slash(0xC0, 2, "RCL", OP_RM8_imm8, &CPU::_RCL_RM8_imm8);
    build_slash(0xC0, 3, "RCR", OP_RM8_imm8, &CPU::_RCR_RM8_imm8);
    build_slash(0xC0, 4, "SHL", OP_RM8_imm8, &CPU::_SHL_RM8_imm8);
    build_slash(0xC0, 5, "SHR", OP_RM8_imm8, &CPU::_SHR_RM8_imm8);
    build_slash(0xC0, 6, "SHL", OP_RM8_imm8, &CPU::_SHL_RM8_imm8); // Undocumented
    build_slash(0xC0, 7, "SAR", OP_RM8_imm8, &CPU::_SAR_RM8_imm8);

    build_slash(0xC1, 0, "ROL", OP_RM16_imm8, &CPU::_ROL_RM16_imm8, OP_RM32_imm8, &CPU::_ROL_RM32_imm8);
    build_slash(0xC1, 1, "ROR", OP_RM16_imm8, &CPU::_ROR_RM16_imm8, OP_RM32_imm8, &CPU::_ROR_RM32_imm8);
    build_slash(0xC1, 2, "RCL", OP_RM16_imm8, &CPU::_RCL_RM16_imm8, OP_RM32_imm8, &CPU::_RCL_RM32_imm8);
    build_slash(0xC1, 3, "RCR", OP_RM16_imm8, &CPU::_RCR_RM16_imm8, OP_RM32_imm8, &CPU::_RCR_RM32_imm8);
    build_slash(0xC1, 4, "SHL", OP_RM16_imm8, &CPU::_SHL_RM16_imm8, OP_RM32_imm8, &CPU::_SHL_RM32_imm8);
    build_slash(0xC1, 5, "SHR", OP_RM16_imm8, &CPU::_SHR_RM16_imm8, OP_RM32_imm8, &CPU::_SHR_RM32_imm8);
    build_slash(0xC1, 6, "SHL", OP_RM16_imm8, &CPU::_SHL_RM16_imm8, OP_RM32_imm8, &CPU::_SHL_RM32_imm8); // Undocumented
    build_slash(0xC1, 7, "SAR", OP_RM16_imm8, &CPU::_SAR_RM16_imm8, OP_RM32_imm8, &CPU::_SAR_RM32_imm8);

    build_slash(0xD0, 0, "ROL", OP_RM8_1, &CPU::_ROL_RM8_1);
    build_slash(0xD0, 1, "ROR", OP_RM8_1, &CPU::_ROR_RM8_1);
    build_slash(0xD0, 2, "RCL", OP_RM8_1, &CPU::_RCL_RM8_1);
    build_slash(0xD0, 3, "RCR", OP_RM8_1, &CPU::_RCR_RM8_1);
    build_slash(0xD0, 4, "SHL", OP_RM8_1, &CPU::_SHL_RM8_1);
    build_slash(0xD0, 5, "SHR", OP_RM8_1, &CPU::_SHR_RM8_1);
    build_slash(0xD0, 6, "SHL", OP_RM8_1, &CPU::_SHL_RM8_1); // Undocumented
    build_slash(0xD0, 7, "SAR", OP_RM8_1, &CPU::_SAR_RM8_1);

    build_slash(0xD1, 0, "ROL", OP_RM16_1, &CPU::_ROL_RM16_1, OP_RM32_1, &CPU::_ROL_RM32_1);
    build_slash(0xD1, 1, "ROR", OP_RM16_1, &CPU::_ROR_RM16_1, OP_RM32_1, &CPU::_ROR_RM32_1);
    build_slash(0xD1, 2, "RCL", OP_RM16_1, &CPU::_RCL_RM16_1, OP_RM32_1, &CPU::_RCL_RM32_1);
    build_slash(0xD1, 3, "RCR", OP_RM16_1, &CPU::_RCR_RM16_1, OP_RM32_1, &CPU::_RCR_RM32_1);
    build_slash(0xD1, 4, "SHL", OP_RM16_1, &CPU::_SHL_RM16_1, OP_RM32_1, &CPU::_SHL_RM32_1);
    build_slash(0xD1, 5, "SHR", OP_RM16_1, &CPU::_SHR_RM16_1, OP_RM32_1, &CPU::_SHR_RM32_1);
    build_slash(0xD1, 6, "SHL", OP_RM16_1, &CPU::_SHL_RM16_1, OP_RM32_1, &CPU::_SHL_RM32_1); // Undocumented
    build_slash(0xD1, 7, "SAR", OP_RM16_1, &CPU::_SAR_RM16_1, OP_RM32_1, &CPU::_SAR_RM32_1);

    build_slash(0xD2, 0, "ROL", OP_RM8_CL, &CPU::_ROL_RM8_CL);
    build_slash(0xD2, 1, "ROR", OP_RM8_CL, &CPU::_ROR_RM8_CL);
    build_slash(0xD2, 2, "RCL", OP_RM8_CL, &CPU::_RCL_RM8_CL);
    build_slash(0xD2, 3, "RCR", OP_RM8_CL, &CPU::_RCR_RM8_CL);
    build_slash(0xD2, 4, "SHL", OP_RM8_CL, &CPU::_SHL_RM8_CL);
    build_slash(0xD2, 5, "SHR", OP_RM8_CL, &CPU::_SHR_RM8_CL);
    build_slash(0xD2, 6, "SHL", OP_RM8_CL, &CPU::_SHL_RM8_CL); // Undocumented
    build_slash(0xD2, 7, "SAR", OP_RM8_CL, &CPU::_SAR_RM8_CL);

    build_slash(0xD3, 0, "ROL", OP_RM16_CL, &CPU::_ROL_RM16_CL, OP_RM32_CL, &CPU::_ROL_RM32_CL);
    build_slash(0xD3, 1, "ROR", OP_RM16_CL, &CPU::_ROR_RM16_CL, OP_RM32_CL, &CPU::_ROR_RM32_CL);
    build_slash(0xD3, 2, "RCL", OP_RM16_CL, &CPU::_RCL_RM16_CL, OP_RM32_CL, &CPU::_RCL_RM32_CL);
    build_slash(0xD3, 3, "RCR", OP_RM16_CL, &CPU::_RCR_RM16_CL, OP_RM32_CL, &CPU::_RCR_RM32_CL);
    build_slash(0xD3, 4, "SHL", OP_RM16_CL, &CPU::_SHL_RM16_CL, OP_RM32_CL, &CPU::_SHL_RM32_CL);
    build_slash(0xD3, 5, "SHR", OP_RM16_CL, &CPU::_SHR_RM16_CL, OP_RM32_CL, &CPU::_SHR_RM32_CL);
    build_slash(0xD3, 6, "SHL", OP_RM16_CL, &CPU::_SHL_RM16_CL, OP_RM32_CL, &CPU::_SHL_RM32_CL); // Undocumented
    build_slash(0xD3, 7, "SAR", OP_RM16_CL, &CPU::_SAR_RM16_CL, OP_RM32_CL, &CPU::_SAR_RM32_CL);

    build_slash(0xF6, 0, "TEST", OP_RM8_imm8, &CPU::_TEST_RM8_imm8);
    build_slash(0xF6, 1, "TEST", OP_RM8_imm8, &CPU::_TEST_RM8_imm8); // Undocumented
    build_slash(0xF6, 2, "NOT", OP_RM8, &CPU::_NOT_RM8, LockPrefixAllowed);
    build_slash(0xF6, 3, "NEG", OP_RM8, &CPU::_NEG_RM8, LockPrefixAllowed);
    build_slash(0xF6, 4, "MUL", OP_RM8, &CPU::_MUL_RM8);
    build_slash(0xF6, 5, "IMUL", OP_RM8, &CPU::_IMUL_RM8);
    build_slash(0xF6, 6, "DIV", OP_RM8, &CPU::_DIV_RM8);
    build_slash(0xF6, 7, "IDIV", OP_RM8, &CPU::_IDIV_RM8);

    build_slash(0xF7, 0, "TEST", OP_RM16_imm16, &CPU::_TEST_RM16_imm16, OP_RM32_imm32, &CPU::_TEST_RM32_imm32);
    build_slash(0xF7, 1, "TEST", OP_RM16_imm16, &CPU::_TEST_RM16_imm16, OP_RM32_imm32, &CPU::_TEST_RM32_imm32); // Undocumented
    build_slash(0xF7, 2, "NOT", OP_RM16, &CPU::_NOT_RM16, OP_RM32, &CPU::_NOT_RM32, LockPrefixAllowed);
    build_slash(0xF7, 3, "NEG", OP_RM16, &CPU::_NEG_RM16, OP_RM32, &CPU::_NEG_RM32, LockPrefixAllowed);
    build_slash(0xF7, 4, "MUL", OP_RM16, &CPU::_MUL_RM16, OP_RM32, &CPU::_MUL_RM32);
    build_slash(0xF7, 5, "IMUL", OP_RM16, &CPU::_IMUL_RM16, OP_RM32, &CPU::_IMUL_RM32);
    build_slash(0xF7, 6, "DIV", OP_RM16, &CPU::_DIV_RM16, OP_RM32, &CPU::_DIV_RM32);
    build_slash(0xF7, 7, "IDIV", OP_RM16, &CPU::_IDIV_RM16, OP_RM32, &CPU::_IDIV_RM32);

    build_slash(0xFE, 0, "INC", OP_RM8, &CPU::_INC_RM8, LockPrefixAllowed);
    build_slash(0xFE, 1, "DEC", OP_RM8, &CPU::_DEC_RM8, LockPrefixAllowed);

    build_slash(0xFF, 0, "INC", OP_RM16, &CPU::_INC_RM16, OP_RM32, &CPU::_INC_RM32, LockPrefixAllowed);
    build_slash(0xFF, 1, "DEC", OP_RM16, &CPU::_DEC_RM16, OP_RM32, &CPU::_DEC_RM32, LockPrefixAllowed);
    build_slash(0xFF, 2, "CALL", OP_RM16, &CPU::_CALL_RM16, OP_RM32, &CPU::_CALL_RM32);
    build_slash(0xFF, 3, "CALL", OP_FAR_mem16, &CPU::_CALL_FAR_mem16, OP_FAR_mem32, &CPU::_CALL_FAR_mem32);
    build_slash(0xFF, 4, "JMP", OP_RM16, &CPU::_JMP_RM16, OP_RM32, &CPU::_JMP_RM32);
    build_slash(0xFF, 5, "JMP", OP_FAR_mem16, &CPU::_JMP_FAR_mem16, OP_FAR_mem32, &CPU::_JMP_FAR_mem32);
    build_slash(0xFF, 6, "PUSH", OP_RM16, &CPU::_PUSH_RM16, OP_RM32, &CPU::_PUSH_RM32);

    // Instructions starting with 0x0F are multi-byte opcodes.
    build_0f_slash(0x00, 0, "SLDT", OP_RM16, &CPU::_SLDT_RM16);
    build_0f_slash(0x00, 1, "STR", OP_RM16, &CPU::_STR_RM16);
    build_0f_slash(0x00, 2, "LLDT", OP_RM16, &CPU::_LLDT_RM16);
    build_0f_slash(0x00, 3, "LTR", OP_RM16, &CPU::_LTR_RM16);
    build_0f_slash(0x00, 4, "VERR", OP_RM16, &CPU::_VERR_RM16);
    build_0f_slash(0x00, 5, "VERW", OP_RM16, &CPU::_VERW_RM16);

    build_0f_slash(0x01, 0, "SGDT", OP_RM16, &CPU::_SGDT);
    build_0f_slash(0x01, 1, "SIDT", OP_RM16, &CPU::_SIDT);
    build_0f_slash(0x01, 2, "LGDT", OP_RM16, &CPU::_LGDT);
    build_0f_slash(0x01, 3, "LIDT", OP_RM16, &CPU::_LIDT);
    build_0f_slash(0x01, 4, "SMSW", OP_RM16, &CPU::_SMSW_RM16);
    build_0f_slash(0x01, 6, "LMSW", OP_RM16, &CPU::_LMSW_RM16);
    build_0f_slash(0x01, 7, "INVLPG", OP_RM32, &CPU::_INVLPG);

    build_0f_slash(0xBA, 4, "BT", OP_RM16_imm8, &CPU::_BT_RM16_imm8, OP_RM32_imm8, &CPU::_BT_RM32_imm8, LockPrefixAllowed);
    build_0f_slash(0xBA, 5, "BTS", OP_RM16_imm8, &CPU::_BTS_RM16_imm8, OP_RM32_imm8, &CPU::_BTS_RM32_imm8, LockPrefixAllowed);
    build_0f_slash(0xBA, 6, "BTR", OP_RM16_imm8, &CPU::_BTR_RM16_imm8, OP_RM32_imm8, &CPU::_BTR_RM32_imm8, LockPrefixAllowed);
    build_0f_slash(0xBA, 7, "BTC", OP_RM16_imm8, &CPU::_BTC_RM16_imm8, OP_RM32_imm8, &CPU::_BTC_RM32_imm8, LockPrefixAllowed);

    build_0f(0x02, "LAR", OP_reg16_RM16, &CPU::_LAR_reg16_RM16, OP_reg32_RM32, &CPU::_LAR_reg32_RM32);
    build_0f(0x03, "LSL", OP_reg16_RM16, &CPU::_LSL_reg16_RM16, OP_reg32_RM32, &CPU::_LSL_reg32_RM32);
    build_0f(0x06, "CLTS", OP, &CPU::_CLTS);
    build_0f(0x09, "WBINVD", OP, &CPU::_WBINVD);
    build_0f(0x0B, "UD2", OP, &CPU::_UD2);

    build_0f(0x1E, "NOP", OP_RM16, &CPU::_NOP);

    build_0f(0x20, "MOV", OP_reg32_CR, &CPU::_MOV_reg32_CR);
    build_0f(0x21, "MOV", OP_reg32_DR, &CPU::_MOV_reg32_DR);
    build_0f(0x22, "MOV", OP_CR_reg32, &CPU::_MOV_CR_reg32);
    build_0f(0x23, "MOV", OP_DR_reg32, &CPU::_MOV_DR_reg32);

    build_0f(0x31, "RDTSC", OP, &CPU::_RDTSC);

    build_0f(0x40, "CMOVO", OP_reg16_RM16, &CPU::_CMOVcc_reg16_RM16, OP_reg32_RM32, &CPU::_CMOVcc_reg32_RM32);
    build_0f(0x41, "CMOVNO", OP_reg16_RM16, &CPU::_CMOVcc_reg16_RM16, OP_reg32_RM32, &CPU::_CMOVcc_reg32_RM32);
    build_0f(0x42, "CMOVC", OP_reg16_RM16, &CPU::_CMOVcc_reg16_RM16, OP_reg32_RM32, &CPU::_CMOVcc_reg32_RM32);
    build_0f(0x43, "CMOVNC", OP_reg16_RM16, &CPU::_CMOVcc_reg16_RM16, OP_reg32_RM32, &CPU::_CMOVcc_reg32_RM32);
    build_0f(0x44, "CMOVZ", OP_reg16_RM16, &CPU::_CMOVcc_reg16_RM16, OP_reg32_RM32, &CPU::_CMOVcc_reg32_RM32);
    build_0f(0x45, "CMOVNZ", OP_reg16_RM16, &CPU::_CMOVcc_reg16_RM16, OP_reg32_RM32, &CPU::_CMOVcc_reg32_RM32);
    build_0f(0x46, "CMOVNA", OP_reg16_RM16, &CPU::_CMOVcc_reg16_RM16, OP_reg32_RM32, &CPU::_CMOVcc_reg32_RM32);
    build_0f(0x47, "CMOVA", OP_reg16_RM16, &CPU::_CMOVcc_reg16_RM16, OP_reg32_RM32, &CPU::_CMOVcc_reg32_RM32);
    build_0f(0x48, "CMOVS", OP_reg16_RM16, &CPU::_CMOVcc_reg16_RM16, OP_reg32_RM32, &CPU::_CMOVcc_reg32_RM32);
    build_0f(0x49, "CMOVNS", OP_reg16_RM16, &CPU::_CMOVcc_reg16_RM16, OP_reg32_RM32, &CPU::_CMOVcc_reg32_RM32);
    build_0f(0x4A, "CMOVP", OP_reg16_RM16, &CPU::_CMOVcc_reg16_RM16, OP_reg32_RM32, &CPU::_CMOVcc_reg32_RM32);
    build_0f(0x4B, "CMOVNP", OP_reg16_RM16, &CPU::_CMOVcc_reg16_RM16, OP_reg32_RM32, &CPU::_CMOVcc_reg32_RM32);
    build_0f(0x4C, "CMOVL", OP_reg16_RM16, &CPU::_CMOVcc_reg16_RM16, OP_reg32_RM32, &CPU::_CMOVcc_reg32_RM32);
    build_0f(0x4D, "CMOVNL", OP_reg16_RM16, &CPU::_CMOVcc_reg16_RM16, OP_reg32_RM32, &CPU::_CMOVcc_reg32_RM32);
    build_0f(0x4E, "CMOVNG", OP_reg16_RM16, &CPU::_CMOVcc_reg16_RM16, OP_reg32_RM32, &CPU::_CMOVcc_reg32_RM32);
    build_0f(0x4F, "CMOVG", OP_reg16_RM16, &CPU::_CMOVcc_reg16_RM16, OP_reg32_RM32, &CPU::_CMOVcc_reg32_RM32);

    build_0f(0x80, "JO", OP_NEAR_imm, &CPU::_Jcc_NEAR_imm);
    build_0f(0x81, "JNO", OP_NEAR_imm, &CPU::_Jcc_NEAR_imm);
    build_0f(0x82, "JC", OP_NEAR_imm, &CPU::_Jcc_NEAR_imm);
    build_0f(0x83, "JNC", OP_NEAR_imm, &CPU::_Jcc_NEAR_imm);
    build_0f(0x84, "JZ", OP_NEAR_imm, &CPU::_Jcc_NEAR_imm);
    build_0f(0x85, "JNZ", OP_NEAR_imm, &CPU::_Jcc_NEAR_imm);
    build_0f(0x86, "JNA", OP_NEAR_imm, &CPU::_Jcc_NEAR_imm);
    build_0f(0x87, "JA", OP_NEAR_imm, &CPU::_Jcc_NEAR_imm);
    build_0f(0x88, "JS", OP_NEAR_imm, &CPU::_Jcc_NEAR_imm);
    build_0f(0x89, "JNS", OP_NEAR_imm, &CPU::_Jcc_NEAR_imm);
    build_0f(0x8A, "JP", OP_NEAR_imm, &CPU::_Jcc_NEAR_imm);
    build_0f(0x8B, "JNP", OP_NEAR_imm, &CPU::_Jcc_NEAR_imm);
    build_0f(0x8C, "JL", OP_NEAR_imm, &CPU::_Jcc_NEAR_imm);
    build_0f(0x8D, "JNL", OP_NEAR_imm, &CPU::_Jcc_NEAR_imm);
    build_0f(0x8E, "JNG", OP_NEAR_imm, &CPU::_Jcc_NEAR_imm);
    build_0f(0x8F, "JG", OP_NEAR_imm, &CPU::_Jcc_NEAR_imm);

    build_0f(0x90, "SETO", OP_RM8, &CPU::_SETcc_RM8);
    build_0f(0x91, "SETNO", OP_RM8, &CPU::_SETcc_RM8);
    build_0f(0x92, "SETC", OP_RM8, &CPU::_SETcc_RM8);
    build_0f(0x93, "SETNC", OP_RM8, &CPU::_SETcc_RM8);
    build_0f(0x94, "SETZ", OP_RM8, &CPU::_SETcc_RM8);
    build_0f(0x95, "SETNZ", OP_RM8, &CPU::_SETcc_RM8);
    build_0f(0x96, "SETNA", OP_RM8, &CPU::_SETcc_RM8);
    build_0f(0x97, "SETA", OP_RM8, &CPU::_SETcc_RM8);
    build_0f(0x98, "SETS", OP_RM8, &CPU::_SETcc_RM8);
    build_0f(0x99, "SETNS", OP_RM8, &CPU::_SETcc_RM8);
    build_0f(0x9A, "SETP", OP_RM8, &CPU::_SETcc_RM8);
    build_0f(0x9B, "SETNP", OP_RM8, &CPU::_SETcc_RM8);
    build_0f(0x9C, "SETL", OP_RM8, &CPU::_SETcc_RM8);
    build_0f(0x9D, "SETNL", OP_RM8, &CPU::_SETcc_RM8);
    build_0f(0x9E, "SETNG", OP_RM8, &CPU::_SETcc_RM8);
    build_0f(0x9F, "SETG", OP_RM8, &CPU::_SETcc_RM8);

    build_0f(0xA0, "PUSH", OP_FS, &CPU::_PUSH_FS);
    build_0f(0xA1, "POP", OP_FS, &CPU::_POP_FS);
    build_0f(0xA2, "CPUID", OP, &CPU::_CPUID);
    build_0f(0xA3, "BT", OP_RM16_reg16, &CPU::_BT_RM16_reg16, OP_RM32_reg32, &CPU::_BT_RM32_reg32);
    build_0f(0xA4, "SHLD", OP_RM16_reg16_imm8, &CPU::_SHLD_RM16_reg16_imm8, OP_RM32_reg32_imm8, &CPU::_SHLD_RM32_reg32_imm8);
    build_0f(0xA5, "SHLD", OP_RM16_reg16_CL, &CPU::_SHLD_RM16_reg16_CL, OP_RM32_reg32_CL, &CPU::_SHLD_RM32_reg32_CL);
    build_0f(0xA8, "PUSH", OP_GS, &CPU::_PUSH_GS);
    build_0f(0xA9, "POP", OP_GS, &CPU::_POP_GS);
    build_0f(0xAB, "BTS", OP_RM16_reg16, &CPU::_BTS_RM16_reg16, OP_RM32_reg32, &CPU::_BTS_RM32_reg32);
    build_0f(0xAC, "SHRD", OP_RM16_reg16_imm8, &CPU::_SHRD_RM16_reg16_imm8, OP_RM32_reg32_imm8, &CPU::_SHRD_RM32_reg32_imm8);
    build_0f(0xAD, "SHRD", OP_RM16_reg16_CL, &CPU::_SHRD_RM16_reg16_CL, OP_RM32_reg32_CL, &CPU::_SHRD_RM32_reg32_CL);
    build_0f(0xAF, "IMUL", OP_reg16_RM16, &CPU::_IMUL_reg16_RM16, OP_reg32_RM32, &CPU::_IMUL_reg32_RM32);
    build_0f(0xB1, "CMPXCHG", OP_RM16_reg16, &CPU::_CMPXCHG_RM16_reg16, OP_RM32_reg32, &CPU::_CMPXCHG_RM32_reg32);
    build_0f(0xB2, "LSS", OP_reg16_mem16, &CPU::_LSS_reg16_mem16, OP_reg32_mem32, &CPU::_LSS_reg32_mem32);
    build_0f(0xB3, "BTR", OP_RM16_reg16, &CPU::_BTR_RM16_reg16, OP_RM32_reg32, &CPU::_BTR_RM32_reg32);
    build_0f(0xB4, "LFS", OP_reg16_mem16, &CPU::_LFS_reg16_mem16, OP_reg32_mem32, &CPU::_LFS_reg32_mem32);
    build_0f(0xB5, "LGS", OP_reg16_mem16, &CPU::_LGS_reg16_mem16, OP_reg32_mem32, &CPU::_LGS_reg32_mem32);
    build_0f(0xB6, "MOVZX", OP_reg16_RM8, &CPU::_MOVZX_reg16_RM8, OP_reg32_RM8, &CPU::_MOVZX_reg32_RM8);
    build_0f(0xB7, "0xB7", OP, nullptr, "MOVZX", OP_reg32_RM16, &CPU::_MOVZX_reg32_RM16);
    build_0f(0xB9, "UD1", OP, &CPU::_UD1);
    build_0f(0xBB, "BTC", OP_RM16_reg16, &CPU::_BTC_RM16_reg16, OP_RM32_reg32, &CPU::_BTC_RM32_reg32);
    build_0f(0xBC, "BSF", OP_reg16_RM16, &CPU::_BSF_reg16_RM16, OP_reg32_RM32, &CPU::_BSF_reg32_RM32);
    build_0f(0xBD, "BSR", OP_reg16_RM16, &CPU::_BSR_reg16_RM16, OP_reg32_RM32, &CPU::_BSR_reg32_RM32);
    build_0f(0xBE, "MOVSX", OP_reg16_RM8, &CPU::_MOVSX_reg16_RM8, OP_reg32_RM8, &CPU::_MOVSX_reg32_RM8);
    build_0f(0xBF, "0xBF", OP, nullptr, "MOVSX", OP_reg32_RM16, &CPU::_MOVSX_reg32_RM16);

    for (u8 i = 0xc8; i <= 0xcf; ++i)
        build_0f(i, "BSWAP", OP_reg32, &CPU::_BSWAP_reg32);

    build_0f(0xFF, "UD0", OP, &CPU::_UD0);

    has_built_tables = true;
}

FLATTEN Instruction Instruction::from_stream(InstructionStream& stream, bool o32, bool a32)
{
    return Instruction(stream, o32, a32);
}

unsigned Instruction::length() const
{
    unsigned len = 1;
    if (m_has_sub_op)
        ++len;
    if (m_has_rm) {
        ++len;
        if (m_modrm.m_has_sib)
            ++len;
        len += m_modrm.m_displacement_bytes;
    }
    len += m_imm1_bytes;
    len += m_imm2_bytes;
    len += m_prefix_bytes;
    return len;
}

static SegmentRegisterIndex to_segment_prefix(u8 op)
{
    switch (op) {
    case 0x26:
        return SegmentRegisterIndex::ES;
    case 0x2e:
        return SegmentRegisterIndex::CS;
    case 0x36:
        return SegmentRegisterIndex::SS;
    case 0x3e:
        return SegmentRegisterIndex::DS;
    case 0x64:
        return SegmentRegisterIndex::FS;
    case 0x65:
        return SegmentRegisterIndex::GS;
    default:
        return SegmentRegisterIndex::None;
    }
}

ALWAYS_INLINE Instruction::Instruction(InstructionStream& stream, bool o32, bool a32)
    : m_a32(a32)
    , m_o32(o32)
{
    for (;; ++m_prefix_bytes) {
        u8 opbyte = stream.read_instruction8();
        if (opbyte == Prefix::OperandSizeOverride) {
            m_o32 = !o32;
            m_has_operand_size_override_prefix = true;
            continue;
        }
        if (opbyte == Prefix::AddressSizeOverride) {
            m_a32 = !a32;
            m_has_address_size_override_prefix = true;
            continue;
        }
        if (opbyte == Prefix::REPZ || opbyte == Prefix::REPNZ) {
            // FIXME: What should we do here? Respect the first or last prefix?
            ASSERT(!m_rep_prefix);
            m_rep_prefix = opbyte;
            continue;
        }
        if (opbyte == Prefix::LOCK) {
            m_has_lock_prefix = true;
            continue;
        }
        auto segment_prefix = to_segment_prefix(opbyte);
        if (segment_prefix != SegmentRegisterIndex::None) {
            m_segment_prefix = segment_prefix;
            continue;
        }
        m_op = opbyte;
        break;
    }

    if (m_op == 0x0F) {
        m_has_sub_op = true;
        m_sub_op = stream.read_instruction8();
        m_descriptor = m_o32 ? &s_0f_table32[m_sub_op] : &s_0f_table16[m_sub_op];
    } else {
        m_descriptor = m_o32 ? &s_table32[m_op] : &s_table16[m_op];
    }

    m_has_rm = m_descriptor->has_rm;
    if (m_has_rm) {
        // Consume ModR/M (may include SIB and displacement.)
        m_modrm.decode(stream, m_a32);
        m_register_index = (m_modrm.m_rm >> 3) & 7;
    } else {
        if (has_sub_op())
            m_register_index = m_sub_op & 7;
        else
            m_register_index = m_op & 7;
    }

    bool hasSlash = m_descriptor->format == MultibyteWithSlash;

    if (hasSlash) {
        m_descriptor = &m_descriptor->slashes[slash()];
    }

    if (UNLIKELY(!m_descriptor->impl)) {
        if (m_has_sub_op) {
            if (hasSlash)
                vlog(LogCPU, "Instruction %02X %02X /%u not understood", m_op, m_sub_op, slash());
            else
                vlog(LogCPU, "Instruction %02X %02X not understood", m_op, m_sub_op);
        } else {
            if (hasSlash)
                vlog(LogCPU, "Instruction %02X /%u not understood", m_op, slash());
            else
                vlog(LogCPU, "Instruction %02X not understood", m_op);
        }
        m_descriptor = nullptr;
        return;
    }

    if (UNLIKELY(m_has_lock_prefix && !m_descriptor->lock_prefix_allowed)) {
        vlog(LogCPU, "Instruction not allowed with LOCK prefix, this will raise #UD.");
        m_descriptor = nullptr;
        return;
    }

    m_impl = m_descriptor->impl;

    m_imm1_bytes = m_descriptor->imm1_bytes_for_address_size(m_a32);
    m_imm2_bytes = m_descriptor->imm2_bytes_for_address_size(m_a32);

    // Consume immediates if present.
    if (m_imm2_bytes)
        m_imm2 = stream.read_bytes(m_imm2_bytes);
    if (m_imm1_bytes)
        m_imm1 = stream.read_bytes(m_imm1_bytes);
}

u32 InstructionStream::read_bytes(unsigned count)
{
    switch (count) {
    case 1:
        return read_instruction8();
    case 2:
        return read_instruction16();
    case 4:
        return read_instruction32();
    }
    ASSERT_NOT_REACHED();
    return 0;
}

const char* Instruction::reg8_name() const
{
    return CPU::register_name(static_cast<CPU::RegisterIndex8>(register_index()));
}

const char* Instruction::reg16_name() const
{
    return CPU::register_name(static_cast<CPU::RegisterIndex16>(register_index()));
}

const char* Instruction::reg32_name() const
{
    return CPU::register_name(static_cast<CPU::RegisterIndex32>(register_index()));
}

QString MemoryOrRegisterReference::to_string_o8() const
{
    if (is_register())
        return CPU::register_name(static_cast<CPU::RegisterIndex8>(m_register_index));
    return QString("[%1]").arg(toString());
}

QString MemoryOrRegisterReference::to_string_o16() const
{
    if (is_register())
        return CPU::register_name(static_cast<CPU::RegisterIndex16>(m_register_index));
    return QString("[%1]").arg(toString());
}

QString MemoryOrRegisterReference::to_string_o32() const
{
    if (is_register())
        return CPU::register_name(static_cast<CPU::RegisterIndex32>(m_register_index));
    return QString("[%1]").arg(toString());
}

QString MemoryOrRegisterReference::toString() const
{
    if (m_a32)
        return toStringA32();
    return toStringA16();
}

QString MemoryOrRegisterReference::toStringA16() const
{
    QString base;
    bool hasDisplacement = false;

    switch (m_rm & 7) {
    case 0:
        base = "bx+si";
        break;
    case 1:
        base = "bx+di";
        break;
    case 2:
        base = "bp+si";
        break;
    case 3:
        base = "bp+di";
        break;
    case 4:
        base = "si";
        break;
    case 5:
        base = "di";
        break;
    case 7:
        base = "bx";
        break;
    case 6:
        if ((m_rm & 0xc0) == 0)
            base.sprintf("0x%04x", m_displacement16);
        else
            base = "bp";
        break;
    }

    switch (m_rm & 0xc0) {
    case 0x40:
    case 0x80:
        hasDisplacement = true;
    }

    if (!hasDisplacement)
        return base;

    QString disp;
    if ((i16)m_displacement16 < 0)
        disp.sprintf("-0x%x", -(i16)m_displacement16);
    else
        disp.sprintf("+0x%x", m_displacement16);
    return QString("%1%2").arg(base).arg(disp);
}

static QString sibToString(u8 rm, u8 sib)
{
    QString scale;
    QString index;
    QString base;
    switch (sib & 0xC0) {
    case 0x00:;
        break;
    case 0x40:
        scale = "*2";
        break;
    case 0x80:
        scale = "*4";
        break;
    case 0xC0:
        scale = "*8";
        break;
    }
    switch ((sib >> 3) & 0x07) {
    case 0:
        index = "eax";
        break;
    case 1:
        index = "ecx";
        break;
    case 2:
        index = "edx";
        break;
    case 3:
        index = "ebx";
        break;
    case 4:
        break;
    case 5:
        index = "ebp";
        break;
    case 6:
        index = "esi";
        break;
    case 7:
        index = "edi";
        break;
    }
    switch (sib & 0x07) {
    case 0:
        base = "eax";
        break;
    case 1:
        base = "ecx";
        break;
    case 2:
        base = "edx";
        break;
    case 3:
        base = "ebx";
        break;
    case 4:
        base = "esp";
        break;
    case 6:
        base = "esi";
        break;
    case 7:
        base = "edi";
        break;
    default: // 5
        switch ((rm >> 6) & 3) {
        case 1:
        case 2:
            base = "ebp";
            break;
        }
        break;
    }
    if (base.isEmpty())
        return QString("%1%2").arg(index).arg(scale);
    return QString("%1+%2%3").arg(base).arg(index).arg(scale);
}

QString MemoryOrRegisterReference::toStringA32() const
{
    if (is_register())
        return CPU::register_name(static_cast<CPU::RegisterIndex32>(m_register_index));

    bool has_displacement = false;
    switch (m_rm & 0xc0) {
    case 0x40:
    case 0x80:
        has_displacement = true;
    }
    if (m_has_sib && (m_sib & 7) == 5)
        has_displacement = true;

    QString base;
    switch (m_rm & 7) {
    case 0:
        base = "eax";
        break;
    case 1:
        base = "ecx";
        break;
    case 2:
        base = "edx";
        break;
    case 3:
        base = "ebx";
        break;
    case 6:
        base = "esi";
        break;
    case 7:
        base = "edi";
        break;
    case 5:
        if ((m_rm & 0xc0) == 0)
            base.sprintf("0x%08x", m_displacement32);
        else
            base = "ebp";
        break;
    case 4:
        base = sibToString(m_rm, m_sib);
        break;
    }

    if (!has_displacement)
        return base;

    QString disp;
    if ((i32)m_displacement32 < 0)
        disp.sprintf("-0x%x", -(i32)m_displacement32);
    else
        disp.sprintf("+0x%x", m_displacement32);
    return QString("%1%2").arg(base).arg(disp);
}

#define IMM8ARGS imm8(), 2, 16, QLatin1Char('0')
#define IMM8_1ARGS imm8_1(), 2, 16, QLatin1Char('0')
#define IMM8_2ARGS imm8_2(), 2, 16, QLatin1Char('0')
#define IMM16ARGS imm16(), 4, 16, QLatin1Char('0')
#define IMM16_1ARGS imm16_1(), 4, 16, QLatin1Char('0')
#define IMM16_2ARGS imm16_2(), 4, 16, QLatin1Char('0')
#define IMM32ARGS imm32(), 8, 16, QLatin1Char('0')
#define IMM32_1ARGS imm32_1(), 8, 16, QLatin1Char('0')
#define IMM32_2ARGS imm32_2(), 8, 16, QLatin1Char('0')
#define ADDRARGS m_a32 ? imm32() : imm16(), m_a32 ? 8 : 4, 16, QLatin1Char('0')
#define RM8ARGS m_modrm.to_string_o8()
#define RM16ARGS m_modrm.to_string_o16()
#define RM32ARGS m_modrm.to_string_o32()
#define SEGARGS CPU::register_name(segment_register_index())
#define CDRARGS register_index()

static QString relative_address(u32 origin, bool x32, i8 imm)
{
    QString s;
    if (x32)
        return s.sprintf("%08x", origin + imm);
    u16 w = origin & 0xffff;
    return s.sprintf("%04x", w + imm);
}

static QString relative_address(u32 origin, bool x32, i32 imm)
{
    QString s;
    if (x32)
        return s.sprintf("%08x", origin + imm);
    u16 w = origin & 0xffff;
    i16 si = imm;
    return s.sprintf("%04x", w + si);
}

QString Instruction::to_string(u32 origin, bool x32) const
{
    QString segment_prefix;
    QString asize_prefix;
    QString osize_prefix;
    QString rep_prefix;
    QString lock_prefix;
    if (has_segment_prefix()) {
        segment_prefix = QString("%1: ").arg(CPU::register_name(m_segment_prefix));
    }
    if (has_address_size_override_prefix()) {
        asize_prefix = m_a32 ? "a32 " : "a16 ";
    }
    if (has_operand_size_override_prefix()) {
        osize_prefix = m_o32 ? "o32 " : "o16 ";
    }
    if (has_lock_prefix()) {
        lock_prefix = "lock ";
    }
    if (has_rep_prefix()) {
        rep_prefix = m_rep_prefix == Prefix::REPNZ ? "repnz " : "repz ";
    }
    return QString("%1%2%3%4%5%6").arg(segment_prefix).arg(asize_prefix).arg(osize_prefix).arg(lock_prefix).arg(rep_prefix).arg(to_string_internal(origin, x32));
}

#define RELADDRARGS relative_address(origin + (m_a32 ? 6 : 4), x32, i32(m_a32 ? imm32() : imm16()))
#define RELIMM8ARGS relative_address(origin + 2, x32, i8(imm8()))
#define RELIMM16ARGS relative_address(origin + 3, x32, i32(imm16()))
#define RELIMM32ARGS relative_address(origin + 5, x32, i32(imm32()))

QString Instruction::to_string_internal(u32 origin, bool x32) const
{
    QString mnemonic = QString(m_descriptor->mnemonic).toLower();

    switch (m_descriptor->format) {
    case OP_RM8_imm8:
        return QString("%1 %2, 0x%3").arg(mnemonic).arg(RM8ARGS).arg(IMM8ARGS);
    case OP_RM16_imm8:
        return QString("%1 %2, 0x%3").arg(mnemonic).arg(RM16ARGS).arg(IMM8ARGS);
    case OP_RM32_imm8:
        return QString("%1 %2, 0x%3").arg(mnemonic).arg(RM32ARGS).arg(IMM8ARGS);
    case OP_reg16_RM16_imm8:
        return QString("%1 %2, %3, 0x%4").arg(mnemonic).arg(reg16_name()).arg(RM16ARGS).arg(IMM8ARGS);
    case OP_reg32_RM32_imm8:
        return QString("%1 %2, %3, 0x%4").arg(mnemonic).arg(reg32_name()).arg(RM32ARGS).arg(IMM8ARGS);
    case OP_AL_imm8:
        return QString("%1 al, 0x%2").arg(mnemonic).arg(IMM8ARGS);
    case OP_imm8:
        return QString("%1 0x%2").arg(mnemonic).arg(IMM8ARGS);
    case OP_reg8_imm8:
        return QString("%1 %2, 0x%3").arg(mnemonic).arg(reg8_name()).arg(IMM8ARGS);
    case OP_AX_imm8:
        return QString("%1 ax, 0x%2").arg(mnemonic).arg(IMM8ARGS);
    case OP_EAX_imm8:
        return QString("%1 eax, 0x%2").arg(mnemonic).arg(IMM8ARGS);
    case OP_imm8_AL:
        return QString("%1 0x%2, al").arg(mnemonic).arg(IMM8ARGS);
    case OP_imm8_AX:
        return QString("%1 0x%2, ax").arg(mnemonic).arg(IMM8ARGS);
    case OP_imm8_EAX:
        return QString("%1 0x%2, eax").arg(mnemonic).arg(IMM8ARGS);
    case OP_AX_imm16:
        return QString("%1 ax, 0x%2").arg(mnemonic).arg(IMM16ARGS);
    case OP_imm16:
        return QString("%1 0x%2").arg(mnemonic).arg(IMM16ARGS);
    case OP_reg16_imm16:
        return QString("%1 %2, 0x%3").arg(mnemonic).arg(reg16_name()).arg(IMM16ARGS);
    case OP_reg16_RM16_imm16:
        return QString("%1 %2, %3, 0x%4").arg(mnemonic).arg(reg16_name()).arg(RM16ARGS).arg(IMM16ARGS);
    case OP_reg32_RM32_imm32:
        return QString("%1 %2, %3, 0x%4").arg(mnemonic).arg(reg32_name()).arg(RM32ARGS).arg(IMM32ARGS);
    case OP_imm32:
        return QString("%1 0x%2").arg(mnemonic).arg(IMM32ARGS);
    case OP_EAX_imm32:
        return QString("%1 eax, 0x%2").arg(mnemonic).arg(IMM32ARGS);
    case OP_CS:
        return QString("%1 cs").arg(mnemonic);
    case OP_DS:
        return QString("%1 ds").arg(mnemonic);
    case OP_ES:
        return QString("%1 es").arg(mnemonic);
    case OP_SS:
        return QString("%1 ss").arg(mnemonic);
    case OP_FS:
        return QString("%1 fs").arg(mnemonic);
    case OP_GS:
        return QString("%1 gs").arg(mnemonic);
    case OP:
        return QString("%1").arg(mnemonic);
    case OP_reg32:
        return QString("%1 %2").arg(mnemonic).arg(reg32_name());
    case OP_imm8_imm16:
        return QString("%1 0x%2, 0x%3").arg(mnemonic).arg(IMM8_1ARGS).arg(IMM16_2ARGS);
    case OP_moff8_AL:
        return QString("%1 [0x%2], al").arg(mnemonic).arg(ADDRARGS);
    case OP_moff16_AX:
        return QString("%1 [0x%2], ax").arg(mnemonic).arg(ADDRARGS);
    case OP_moff32_EAX:
        return QString("%1 [0x%2], eax").arg(mnemonic).arg(ADDRARGS);
    case OP_AL_moff8:
        return QString("%1 al, [0x%2]").arg(mnemonic).arg(ADDRARGS);
    case OP_AX_moff16:
        return QString("%1 ax, [0x%2]").arg(mnemonic).arg(ADDRARGS);
    case OP_EAX_moff32:
        return QString("%1 eax, [0x%2]").arg(mnemonic).arg(ADDRARGS);
    case OP_imm16_imm16:
        return QString("%1 0x%2:0x%3").arg(mnemonic).arg(IMM16_1ARGS).arg(IMM16_2ARGS);
    case OP_imm16_imm32:
        return QString("%1 0x%2:0x%3").arg(mnemonic).arg(IMM16_1ARGS).arg(IMM32_2ARGS);
    case OP_reg32_imm32:
        return QString("%1 %2, 0x%3").arg(mnemonic).arg(reg32_name()).arg(IMM32ARGS);
    case OP_RM8_1:
        return QString("%1 %2, 1").arg(mnemonic).arg(RM8ARGS);
    case OP_RM16_1:
        return QString("%1 %2, 1").arg(mnemonic).arg(RM16ARGS);
    case OP_RM32_1:
        return QString("%1 %2, 1").arg(mnemonic).arg(RM32ARGS);
    case OP_RM8_CL:
        return QString("%1 %2, cl").arg(mnemonic).arg(RM8ARGS);
    case OP_RM16_CL:
        return QString("%1 %2, cl").arg(mnemonic).arg(RM16ARGS);
    case OP_RM32_CL:
        return QString("%1 %2, cl").arg(mnemonic).arg(RM32ARGS);
    case OP_reg16:
        return QString("%1 %2").arg(mnemonic).arg(reg16_name());
    case OP_AX_reg16:
        return QString("%1 ax, %2").arg(mnemonic).arg(reg16_name());
    case OP_EAX_reg32:
        return QString("%1 eax, %2").arg(mnemonic).arg(reg32_name());
    case OP_3:
        return QString("%1 3").arg(mnemonic);
    case OP_AL_DX:
        return QString("%1 al, dx").arg(mnemonic);
    case OP_AX_DX:
        return QString("%1 ax, dx").arg(mnemonic);
    case OP_EAX_DX:
        return QString("%1 eax, dx").arg(mnemonic);
    case OP_DX_AL:
        return QString("%1 dx, al").arg(mnemonic);
    case OP_DX_AX:
        return QString("%1 dx, ax").arg(mnemonic);
    case OP_DX_EAX:
        return QString("%1 dx, eax").arg(mnemonic);
    case OP_reg8_CL:
        return QString("%1 %2, cl").arg(mnemonic).arg(reg8_name());
    case OP_RM8:
        return QString("%1 %2").arg(mnemonic).arg(RM8ARGS);
    case OP_RM16:
        return QString("%1 %2").arg(mnemonic).arg(RM16ARGS);
    case OP_RM32:
        return QString("%1 %2").arg(mnemonic).arg(RM32ARGS);
    case OP_RM8_reg8:
        return QString("%1 %2, %3").arg(mnemonic).arg(RM8ARGS).arg(reg8_name());
    case OP_RM16_reg16:
        return QString("%1 %2, %3").arg(mnemonic).arg(RM16ARGS).arg(reg16_name());
    case OP_RM32_reg32:
        return QString("%1 %2, %3").arg(mnemonic).arg(RM32ARGS).arg(reg32_name());
    case OP_reg8_RM8:
        return QString("%1 %2, %3").arg(mnemonic).arg(reg8_name()).arg(RM8ARGS);
    case OP_reg16_RM16:
        return QString("%1 %2, %3").arg(mnemonic).arg(reg16_name()).arg(RM16ARGS);
    case OP_reg32_RM32:
        return QString("%1 %2, %3").arg(mnemonic).arg(reg32_name()).arg(RM32ARGS);
    case OP_reg32_RM16:
        return QString("%1 %2, %3").arg(mnemonic).arg(reg32_name()).arg(RM16ARGS);
    case OP_reg16_RM8:
        return QString("%1 %2, %3").arg(mnemonic).arg(reg16_name()).arg(RM8ARGS);
    case OP_reg32_RM8:
        return QString("%1 %2, %3").arg(mnemonic).arg(reg32_name()).arg(RM8ARGS);
    case OP_RM16_imm16:
        return QString("%1 %2, 0x%3").arg(mnemonic).arg(RM16ARGS).arg(IMM16ARGS);
    case OP_RM32_imm32:
        return QString("%1 %2, 0x%3").arg(mnemonic).arg(RM32ARGS).arg(IMM32ARGS);
    case OP_RM16_seg:
        return QString("%1 %2, %3").arg(mnemonic).arg(RM16ARGS).arg(SEGARGS);
    case OP_RM32_seg:
        return QString("%1 %2, %3").arg(mnemonic).arg(RM32ARGS).arg(SEGARGS);
    case OP_seg_RM16:
        return QString("%1 %2, %3").arg(mnemonic).arg(SEGARGS).arg(RM16ARGS);
    case OP_seg_RM32:
        return QString("%1 %2, %3").arg(mnemonic).arg(SEGARGS).arg(RM32ARGS);
    case OP_reg16_mem16:
        return QString("%1 %2, %3").arg(mnemonic).arg(reg16_name()).arg(RM16ARGS);
    case OP_reg32_mem32:
        return QString("%1 %2, %3").arg(mnemonic).arg(reg32_name()).arg(RM32ARGS);
    case OP_FAR_mem16:
        return QString("%1 far %2").arg(mnemonic).arg(RM16ARGS);
    case OP_FAR_mem32:
        return QString("%1 far %2").arg(mnemonic).arg(RM32ARGS);
    case OP_reg32_CR:
        return QString("%1 %2, cr%3").arg(mnemonic).arg(CPU::register_name(static_cast<CPU::RegisterIndex32>(rm() & 7))).arg(CDRARGS);
    case OP_CR_reg32:
        return QString("%1 cr%2, %3").arg(mnemonic).arg(CDRARGS).arg(CPU::register_name(static_cast<CPU::RegisterIndex32>(rm() & 7)));
    case OP_reg32_DR:
        return QString("%1 %2, dr%3").arg(mnemonic).arg(CPU::register_name(static_cast<CPU::RegisterIndex32>(rm() & 7))).arg(CDRARGS);
    case OP_DR_reg32:
        return QString("%1 dr%2, %3").arg(mnemonic).arg(CDRARGS).arg(CPU::register_name(static_cast<CPU::RegisterIndex32>(rm() & 7)));
    case OP_short_imm8:
        return QString("%1 short 0x%2").arg(mnemonic).arg(RELIMM8ARGS);
    case OP_relimm16:
        return QString("%1 0x%2").arg(mnemonic).arg(RELIMM16ARGS);
    case OP_relimm32:
        return QString("%1 0x%2").arg(mnemonic).arg(RELIMM32ARGS);
    case OP_NEAR_imm:
        return QString("%1 near 0x%2").arg(mnemonic).arg(RELADDRARGS);
    case OP_RM16_reg16_imm8:
        return QString("%1 %2, %3, 0x%4").arg(mnemonic).arg(RM16ARGS).arg(reg16_name()).arg(IMM8ARGS);
    case OP_RM32_reg32_imm8:
        return QString("%1 %2, %3, 0x%4").arg(mnemonic).arg(RM32ARGS).arg(reg32_name()).arg(IMM8ARGS);
    case OP_RM16_reg16_CL:
        return QString("%1 %2, %3, cl").arg(mnemonic).arg(RM16ARGS).arg(reg16_name());
    case OP_RM32_reg32_CL:
        return QString("%1 %2, %3, cl").arg(mnemonic).arg(RM32ARGS).arg(reg32_name());
    case InstructionPrefix:
        return mnemonic;
    case InvalidFormat:
    case MultibyteWithSlash:
    case MultibyteWithSubopcode:
    case __BeginFormatsWithRMByte:
    case __EndFormatsWithRMByte:
        return QString("(!%1)").arg(mnemonic);
    }
    ASSERT_NOT_REACHED();
    return QString();
}

QString Instruction::mnemonic() const
{
    if (!m_descriptor) {
        ASSERT_NOT_REACHED();
    }
    return m_descriptor->mnemonic;
}

u16 SimpleInstructionStream::read_instruction16()
{
    u8 lsb = *(m_data++);
    u8 msb = *(m_data++);
    return weld<u16>(msb, lsb);
}

u32 SimpleInstructionStream::read_instruction32()
{
    u16 lsw = read_instruction16();
    u16 msw = read_instruction16();
    return weld<u32>(msw, lsw);
}
