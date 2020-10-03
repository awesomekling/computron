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

#define DEFINE_INSTRUCTION_HANDLERS_GRP1(op)                             \
    DEFAULT_RM8_reg8(op)                                                 \
        DEFAULT_RM16_reg16(op)                                           \
            DEFAULT_RM32_reg32(op)                                       \
                DEFAULT_reg8_RM8(op)                                     \
                    DEFAULT_reg16_RM16(op)                               \
                        DEFAULT_reg32_RM32(op)                           \
                            DEFAULT_RM8_imm8(op)                         \
                                DEFAULT_RM16_imm16(op)                   \
                                    DEFAULT_RM32_imm32(op)               \
                                        DEFAULT_RM32_imm8(op)            \
                                            DEFAULT_RM16_imm8(op)        \
                                                DEFAULT_AL_imm8(op)      \
                                                    DEFAULT_AX_imm16(op) \
                                                        DEFAULT_EAX_imm32(op)

#define DEFINE_INSTRUCTION_HANDLERS_GRP2(op) \
    DEFAULT_RM16_imm8(op)                    \
        DEFAULT_RM32_imm8(op)                \
            DEFAULT_RM16_reg16(op)           \
                DEFAULT_RM32_reg32(op)

#define DEFINE_INSTRUCTION_HANDLERS_GRP3(op)        \
    DEFAULT_RM8_imm8(op)                            \
        DEFAULT_RM16_imm8(op)                       \
            DEFAULT_RM32_imm8(op)                   \
                DEFAULT_RM8_1(op)                   \
                    DEFAULT_RM16_1(op)              \
                        DEFAULT_RM32_1(op)          \
                            DEFAULT_RM8_CL(op)      \
                                DEFAULT_RM16_CL(op) \
                                    DEFAULT_RM32_CL(op)

#define DEFINE_INSTRUCTION_HANDLERS_GRP4_READONLY(op, name)                     \
    READONLY_RM8_reg8(op, name)                                                 \
        READONLY_RM16_reg16(op, name)                                           \
            READONLY_RM32_reg32(op, name)                                       \
                READONLY_reg8_RM8(op, name)                                     \
                    READONLY_reg16_RM16(op, name)                               \
                        READONLY_reg32_RM32(op, name)                           \
                            READONLY_RM8_imm8(op, name)                         \
                                READONLY_RM16_imm16(op, name)                   \
                                    READONLY_RM32_imm32(op, name)               \
                                        READONLY_RM32_imm8(op, name)            \
                                            READONLY_RM16_imm8(op, name)        \
                                                READONLY_AL_imm8(op, name)      \
                                                    READONLY_AX_imm16(op, name) \
                                                        READONLY_EAX_imm32(op, name)

#define DEFINE_INSTRUCTION_HANDLERS_GRP5_READONLY(op, name) \
    READONLY_RM8_reg8(op, name)                             \
        READONLY_RM16_reg16(op, name)                       \
            READONLY_RM32_reg32(op, name)                   \
                READONLY_RM8_imm8(op, name)                 \
                    READONLY_RM16_imm16(op, name)           \
                        READONLY_RM32_imm32(op, name)       \
                            READONLY_AL_imm8(op, name)      \
                                READONLY_AX_imm16(op, name) \
                                    READONLY_EAX_imm32(op, name)

#define DEFAULT_RM8_reg8(op)                              \
    void CPU::_##op##_RM8_reg8(Instruction& insn)         \
    {                                                     \
        auto& modrm = insn.modrm();                       \
        modrm.write8(do##op(modrm.read8(), insn.reg8())); \
    }

#define DEFAULT_RM16_reg16(op)                               \
    void CPU::_##op##_RM16_reg16(Instruction& insn)          \
    {                                                        \
        auto& modrm = insn.modrm();                          \
        modrm.write16(do##op(modrm.read16(), insn.reg16())); \
    }

#define DEFAULT_reg8_RM8(op)                                     \
    void CPU::_##op##_reg8_RM8(Instruction& insn)                \
    {                                                            \
        insn.reg8() = do##op(insn.reg8(), insn.modrm().read8()); \
    }

#define DEFAULT_reg16_RM16(op)                                      \
    void CPU::_##op##_reg16_RM16(Instruction& insn)                 \
    {                                                               \
        insn.reg16() = do##op(insn.reg16(), insn.modrm().read16()); \
    }

#define DEFAULT_reg32_RM32(op)                                      \
    void CPU::_##op##_reg32_RM32(Instruction& insn)                 \
    {                                                               \
        insn.reg32() = do##op(insn.reg32(), insn.modrm().read32()); \
    }

#define DEFAULT_RM8_imm8(op)                              \
    void CPU::_##op##_RM8_imm8(Instruction& insn)         \
    {                                                     \
        auto& modrm = insn.modrm();                       \
        modrm.write8(do##op(modrm.read8(), insn.imm8())); \
    }

#define DEFAULT_RM16_imm16(op)                               \
    void CPU::_##op##_RM16_imm16(Instruction& insn)          \
    {                                                        \
        auto& modrm = insn.modrm();                          \
        modrm.write16(do##op(modrm.read16(), insn.imm16())); \
    }

#define DEFAULT_RM32_imm32(op)                               \
    void CPU::_##op##_RM32_imm32(Instruction& insn)          \
    {                                                        \
        auto& modrm = insn.modrm();                          \
        modrm.write32(do##op(modrm.read32(), insn.imm32())); \
    }

#define DEFAULT_RM16_imm8(op)                                                    \
    void CPU::_##op##_RM16_imm8(Instruction& insn)                               \
    {                                                                            \
        auto& modrm = insn.modrm();                                              \
        modrm.write16(do##op(modrm.read16(), signExtendedTo<u16>(insn.imm8()))); \
    }

#define DEFAULT_RM32_imm8(op)                                                    \
    void CPU::_##op##_RM32_imm8(Instruction& insn)                               \
    {                                                                            \
        auto& modrm = insn.modrm();                                              \
        modrm.write32(do##op(modrm.read32(), signExtendedTo<u32>(insn.imm8()))); \
    }

#define DEFAULT_AL_imm8(op)                      \
    void CPU::_##op##_AL_imm8(Instruction& insn) \
    {                                            \
        set_al(do##op(get_al(), insn.imm8()));     \
    }

#define DEFAULT_AX_imm16(op)                      \
    void CPU::_##op##_AX_imm16(Instruction& insn) \
    {                                             \
        set_ax(do##op(get_ax(), insn.imm16()));     \
    }

#define DEFAULT_EAX_imm32(op)                      \
    void CPU::_##op##_EAX_imm32(Instruction& insn) \
    {                                              \
        set_eax(do##op(get_eax(), insn.imm32()));    \
    }

#define READONLY_RM8_reg8(op, name)                  \
    void CPU::_##name##_RM8_reg8(Instruction& insn)  \
    {                                                \
        do                                           \
            ##op(insn.modrm().read8(), insn.reg8()); \
    }

#define READONLY_RM16_reg16(op, name)                  \
    void CPU::_##name##_RM16_reg16(Instruction& insn)  \
    {                                                  \
        do                                             \
            ##op(insn.modrm().read16(), insn.reg16()); \
    }

#define READONLY_RM32_reg32(op, name)                  \
    void CPU::_##name##_RM32_reg32(Instruction& insn)  \
    {                                                  \
        do                                             \
            ##op(insn.modrm().read32(), insn.reg32()); \
    }

#define READONLY_reg8_RM8(op, name)                  \
    void CPU::_##name##_reg8_RM8(Instruction& insn)  \
    {                                                \
        do                                           \
            ##op(insn.reg8(), insn.modrm().read8()); \
    }

#define READONLY_reg16_RM16(op, name)                  \
    void CPU::_##name##_reg16_RM16(Instruction& insn)  \
    {                                                  \
        do                                             \
            ##op(insn.reg16(), insn.modrm().read16()); \
    }

#define READONLY_reg32_RM32(op, name)                  \
    void CPU::_##name##_reg32_RM32(Instruction& insn)  \
    {                                                  \
        do                                             \
            ##op(insn.reg32(), insn.modrm().read32()); \
    }

#define READONLY_RM8_imm8(op, name)                  \
    void CPU::_##name##_RM8_imm8(Instruction& insn)  \
    {                                                \
        do                                           \
            ##op(insn.modrm().read8(), insn.imm8()); \
    }

#define READONLY_RM16_imm16(op, name)                  \
    void CPU::_##name##_RM16_imm16(Instruction& insn)  \
    {                                                  \
        do                                             \
            ##op(insn.modrm().read16(), insn.imm16()); \
    }

#define READONLY_RM32_imm8(op, name)                                       \
    void CPU::_##name##_RM32_imm8(Instruction& insn)                       \
    {                                                                      \
        do                                                                 \
            ##op(insn.modrm().read32(), signExtendedTo<u32>(insn.imm8())); \
    }

#define READONLY_RM32_imm32(op, name)                  \
    void CPU::_##name##_RM32_imm32(Instruction& insn)  \
    {                                                  \
        do                                             \
            ##op(insn.modrm().read32(), insn.imm32()); \
    }

#define READONLY_RM16_imm8(op, name)                                       \
    void CPU::_##name##_RM16_imm8(Instruction& insn)                       \
    {                                                                      \
        do                                                                 \
            ##op(insn.modrm().read16(), signExtendedTo<u16>(insn.imm8())); \
    }

#define READONLY_AL_imm8(op, name)                 \
    void CPU::_##name##_AL_imm8(Instruction& insn) \
    {                                              \
        do                                         \
            ##op(get_al(), insn.imm8());            \
    }

#define READONLY_AX_imm16(op, name)                 \
    void CPU::_##name##_AX_imm16(Instruction& insn) \
    {                                               \
        do                                          \
            ##op(get_ax(), insn.imm16());            \
    }

#define READONLY_EAX_imm32(op, name)                 \
    void CPU::_##name##_EAX_imm32(Instruction& insn) \
    {                                                \
        do                                           \
            ##op(get_eax(), insn.imm32());            \
    }

#define DEFAULT_RM32_reg32(op)                               \
    void CPU::_##op##_RM32_reg32(Instruction& insn)          \
    {                                                        \
        auto& modrm = insn.modrm();                          \
        modrm.write32(do##op(modrm.read32(), insn.reg32())); \
    }

#define DEFAULT_RM8_1(op)                       \
    void CPU::_##op##_RM8_1(Instruction& insn)  \
    {                                           \
        auto& modrm = insn.modrm();             \
        modrm.write8(do##op(modrm.read8(), 1)); \
    }

#define DEFAULT_RM16_1(op)                        \
    void CPU::_##op##_RM16_1(Instruction& insn)   \
    {                                             \
        auto& modrm = insn.modrm();               \
        modrm.write16(do##op(modrm.read16(), 1)); \
    }

#define DEFAULT_RM32_1(op)                        \
    void CPU::_##op##_RM32_1(Instruction& insn)   \
    {                                             \
        auto& modrm = insn.modrm();               \
        modrm.write32(do##op(modrm.read32(), 1)); \
    }

#define DEFAULT_RM8_CL(op)                            \
    void CPU::_##op##_RM8_CL(Instruction& insn)       \
    {                                                 \
        auto& modrm = insn.modrm();                   \
        modrm.write8(do##op(modrm.read8(), get_cl())); \
    }

#define DEFAULT_RM16_CL(op)                             \
    void CPU::_##op##_RM16_CL(Instruction& insn)        \
    {                                                   \
        auto& modrm = insn.modrm();                     \
        modrm.write16(do##op(modrm.read16(), get_cl())); \
    }

#define DEFAULT_RM32_CL(op)                             \
    void CPU::_##op##_RM32_CL(Instruction& insn)        \
    {                                                   \
        auto& modrm = insn.modrm();                     \
        modrm.write32(do##op(modrm.read32(), get_cl())); \
    }
