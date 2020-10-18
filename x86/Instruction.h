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

#include "Common.h"
#include "debug.h"
#include "types.h"
#include <QString>

class CPU;
class Instruction;
struct InstructionDescriptor;

typedef void (CPU::*InstructionImpl)(Instruction&);

struct Prefix {
    enum Op {
        OperandSizeOverride = 0x66,
        AddressSizeOverride = 0x67,
        REP = 0xf3,
        REPZ = 0xf3,
        REPNZ = 0xf2,
        LOCK = 0xf0,
    };
};

class InstructionStream {
public:
    virtual u8 read_instruction8() = 0;
    virtual u16 read_instruction16() = 0;
    virtual u32 read_instruction32() = 0;
    u32 read_bytes(unsigned count);
};

class SimpleInstructionStream final : public InstructionStream {
public:
    SimpleInstructionStream(const u8* data)
        : m_data(data)
    {
    }

    virtual u8 read_instruction8() override { return *(m_data++); }
    virtual u16 read_instruction16() override;
    virtual u32 read_instruction32() override;

private:
    const u8* m_data { nullptr };
};

template<typename T>
class RegisterAccessor {
public:
    RegisterAccessor(T& reg)
        : m_reg(reg)
    {
    }
    T get() const { return m_reg; }
    void set(T value) { m_reg = value; }

private:
    T& m_reg;
};

class MemoryOrRegisterReference {
    friend class CPU;
    friend class Instruction;

public:
    template<typename T>
    T read();
    template<typename T>
    void write(T);

    u8 read8();
    u16 read16();
    u32 read32();
    void write8(u8);
    void write16(u16);
    void write32(u32);
    void write_special(u32, bool o32);

    template<typename T>
    class Accessor;
    Accessor<u8> accessor8();
    Accessor<u16> accessor16();
    Accessor<u32> accessor32();

    QString to_string_o8() const;
    QString to_string_o16() const;
    QString to_string_o32() const;

    bool is_register() const { return m_register_index != 0xffffffff; }
    SegmentRegisterIndex segment() const
    {
        ASSERT(!is_register());
        return m_segment;
    }
    u32 offset();

private:
    MemoryOrRegisterReference() { }

    QString to_string() const;
    QString to_string_a16() const;
    QString to_string_a32() const;

    void resolve(CPU&);
    void resolve16();
    void resolve32();

    void decode(InstructionStream&, bool a32);
    void decode16(InstructionStream&);
    void decode32(InstructionStream&);

    u32 evaluateSIB();

    unsigned m_register_index { 0xffffffff };
    SegmentRegisterIndex m_segment { SegmentRegisterIndex::None };
    union {
        u32 m_offset32 { 0 };
        u16 m_offset16;
    };

    u8 m_a32 { false };

    u8 m_rm { 0 };
    u8 m_sib { 0 };
    u8 m_displacement_bytes { 0 };

    union {
        u32 m_displacement32 { 0 };
        u16 m_displacement16;
    };

    bool m_has_sib { false };

    CPU* m_cpu { nullptr };
};

class Instruction {
public:
    static Instruction from_stream(InstructionStream&, bool o32, bool a32);
    ~Instruction() { }

    void execute(CPU&);

    MemoryOrRegisterReference& modrm()
    {
        ASSERT(has_rm());
        return m_modrm;
    }

    bool has_segment_prefix() const { return m_segment_prefix != SegmentRegisterIndex::None; }
    bool has_address_size_override_prefix() const { return m_has_address_size_override_prefix; }
    bool has_operand_size_override_prefix() const { return m_has_operand_size_override_prefix; }
    bool has_lock_prefix() const { return m_has_lock_prefix; }
    bool has_rep_prefix() const { return m_rep_prefix; }
    u8 rep_prefix() const { return m_rep_prefix; }

    bool is_valid() const { return m_descriptor; }

    unsigned length() const;

    QString mnemonic() const;

    u8 op() const { return m_op; }
    u8 sub_op() const { return m_sub_op; }
    u8 rm() const { return m_modrm.m_rm; }
    u8 slash() const
    {
        ASSERT(has_rm());
        return (rm() >> 3) & 7;
    }

    u8 imm8() const
    {
        ASSERT(m_imm1_bytes == 1);
        return m_imm1;
    }
    u16 imm16() const
    {
        ASSERT(m_imm1_bytes == 2);
        return m_imm1;
    }
    u32 imm32() const
    {
        ASSERT(m_imm1_bytes == 4);
        return m_imm1;
    }

    u8 imm8_1() const { return imm8(); }
    u8 imm8_2() const
    {
        ASSERT(m_imm2_bytes == 1);
        return m_imm2;
    }
    u16 imm16_1() const { return imm16(); }
    u16 imm16_2() const
    {
        ASSERT(m_imm2_bytes == 2);
        return m_imm2;
    }
    u32 imm32_1() const { return imm32(); }
    u32 imm32_2() const
    {
        ASSERT(m_imm2_bytes == 4);
        return m_imm2;
    }

    u32 imm_address() const { return m_a32 ? imm32() : imm16(); }

    LogicalAddress imm_address16_16() const { return LogicalAddress(imm16_1(), imm16_2()); }
    LogicalAddress imm_address16_32() const { return LogicalAddress(imm16_1(), imm32_2()); }

    // These functions assume that the Instruction is bound to a CPU.
    u8& reg8();
    u16& reg16();
    u32& reg32();
    u16& segreg();

    template<typename T>
    T& reg();

    bool has_rm() const { return m_has_rm; }
    bool has_sub_op() const { return m_has_sub_op; }

    unsigned register_index() const { return m_register_index; }
    SegmentRegisterIndex segment_register_index() const { return static_cast<SegmentRegisterIndex>(register_index()); }

    u8 cc() const { return m_has_sub_op ? m_sub_op & 0xf : m_op & 0xf; }

    QString to_string(u32 origin, bool x32) const;

private:
    Instruction(InstructionStream&, bool o32, bool a32);

    QString to_string_internal(u32 origin, bool x32) const;

    const char* reg8_name() const;
    const char* reg16_name() const;
    const char* reg32_name() const;

    u8 m_op { 0 };
    u8 m_sub_op { 0 };
    u32 m_imm1 { 0 };
    u32 m_imm2 { 0 };
    u8 m_register_index { 0 };
    bool m_a32 { false };
    bool m_o32 { false };
    bool m_has_lock_prefix { false };

    bool m_has_sub_op { false };
    bool m_has_rm { false };

    unsigned m_imm1_bytes { 0 };
    unsigned m_imm2_bytes { 0 };
    unsigned m_prefix_bytes { 0 };

    SegmentRegisterIndex m_segment_prefix { SegmentRegisterIndex::None };
    bool m_has_operand_size_override_prefix { false };
    bool m_has_address_size_override_prefix { false };
    u8 m_rep_prefix { 0 };

    MemoryOrRegisterReference m_modrm;

    InstructionImpl m_impl;
    InstructionDescriptor* m_descriptor { nullptr };
    CPU* m_cpu { nullptr };
};

template<typename T>
class MemoryOrRegisterReference::Accessor {
public:
    T get() const { return m_modrm.read<T>(); }
    void set(T value) { m_modrm.write<T>(value); }

private:
    friend class MemoryOrRegisterReference;
    explicit Accessor(MemoryOrRegisterReference& modrm)
        : m_modrm(modrm)
    {
    }
    MemoryOrRegisterReference& m_modrm;
};

inline MemoryOrRegisterReference::Accessor<u8> MemoryOrRegisterReference::accessor8() { return Accessor<u8>(*this); }
inline MemoryOrRegisterReference::Accessor<u16> MemoryOrRegisterReference::accessor16() { return Accessor<u16>(*this); }
inline MemoryOrRegisterReference::Accessor<u32> MemoryOrRegisterReference::accessor32() { return Accessor<u32>(*this); }

void build_opcode_tables_if_needed();
