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
    virtual u8 readInstruction8() = 0;
    virtual u16 readInstruction16() = 0;
    virtual u32 readInstruction32() = 0;
    u32 readBytes(unsigned count);
};

class SimpleInstructionStream final : public InstructionStream {
public:
    SimpleInstructionStream(const u8* data)
        : m_data(data)
    {
    }

    virtual u8 readInstruction8() override { return *(m_data++); }
    virtual u16 readInstruction16() override;
    virtual u32 readInstruction32() override;

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
    void writeSpecial(u32, bool o32);

    template<typename T>
    class Accessor;
    Accessor<u8> accessor8();
    Accessor<u16> accessor16();
    Accessor<u32> accessor32();

    QString toStringO8() const;
    QString toStringO16() const;
    QString toStringO32() const;

    bool isRegister() const { return m_registerIndex != 0xffffffff; }
    SegmentRegisterIndex segment() const
    {
        ASSERT(!isRegister());
        return m_segment;
    }
    u32 offset();

private:
    MemoryOrRegisterReference() { }

    QString toString() const;
    QString toStringA16() const;
    QString toStringA32() const;

    void resolve(CPU&);
    void resolve16();
    void resolve32();

    void decode(InstructionStream&, bool a32);
    void decode16(InstructionStream&);
    void decode32(InstructionStream&);

    u32 evaluateSIB();

    unsigned m_registerIndex { 0xffffffff };
    SegmentRegisterIndex m_segment { SegmentRegisterIndex::None };
    union {
        u32 m_offset32 { 0 };
        u16 m_offset16;
    };

    u8 m_a32 { false };

    u8 m_rm { 0 };
    u8 m_sib { 0 };
    u8 m_displacementBytes { 0 };

    union {
        u32 m_displacement32 { 0 };
        u16 m_displacement16;
    };

    bool m_hasSIB { false };

    CPU* m_cpu { nullptr };
};

class Instruction {
public:
    static Instruction fromStream(InstructionStream&, bool o32, bool a32);
    ~Instruction() { }

    void execute(CPU&);

    MemoryOrRegisterReference& modrm()
    {
        ASSERT(hasRM());
        return m_modrm;
    }

    bool hasSegmentPrefix() const { return m_segmentPrefix != SegmentRegisterIndex::None; }
    bool hasAddressSizeOverridePrefix() const { return m_hasAddressSizeOverridePrefix; }
    bool hasOperandSizeOverridePrefix() const { return m_hasOperandSizeOverridePrefix; }
    bool hasLockPrefix() const { return m_hasLockPrefix; }
    bool hasRepPrefix() const { return m_repPrefix; }
    u8 repPrefix() const { return m_repPrefix; }

    bool isValid() const { return m_descriptor; }

    unsigned length() const;

    QString mnemonic() const;

    u8 op() const { return m_op; }
    u8 subOp() const { return m_subOp; }
    u8 rm() const { return m_modrm.m_rm; }
    u8 slash() const
    {
        ASSERT(hasRM());
        return (rm() >> 3) & 7;
    }

    u8 imm8() const
    {
        ASSERT(m_imm1Bytes == 1);
        return m_imm1;
    }
    u16 imm16() const
    {
        ASSERT(m_imm1Bytes == 2);
        return m_imm1;
    }
    u32 imm32() const
    {
        ASSERT(m_imm1Bytes == 4);
        return m_imm1;
    }

    u8 imm8_1() const { return imm8(); }
    u8 imm8_2() const
    {
        ASSERT(m_imm2Bytes == 1);
        return m_imm2;
    }
    u16 imm16_1() const { return imm16(); }
    u16 imm16_2() const
    {
        ASSERT(m_imm2Bytes == 2);
        return m_imm2;
    }
    u32 imm32_1() const { return imm32(); }
    u32 imm32_2() const
    {
        ASSERT(m_imm2Bytes == 4);
        return m_imm2;
    }

    u32 immAddress() const { return m_a32 ? imm32() : imm16(); }

    LogicalAddress immAddress16_16() const { return LogicalAddress(imm16_1(), imm16_2()); }
    LogicalAddress immAddress16_32() const { return LogicalAddress(imm16_1(), imm32_2()); }

    // These functions assume that the Instruction is bound to a CPU.
    u8& reg8();
    u16& reg16();
    u32& reg32();
    u16& segreg();

    template<typename T>
    T& reg();

    bool hasRM() const { return m_hasRM; }
    bool hasSubOp() const { return m_hasSubOp; }

    unsigned registerIndex() const { return m_registerIndex; }
    SegmentRegisterIndex segmentRegisterIndex() const { return static_cast<SegmentRegisterIndex>(registerIndex()); }

    u8 cc() const { return m_hasSubOp ? m_subOp & 0xf : m_op & 0xf; }

    QString toString(u32 origin, bool x32) const;

private:
    Instruction(InstructionStream&, bool o32, bool a32);

    QString toStringInternal(u32 origin, bool x32) const;

    const char* reg8Name() const;
    const char* reg16Name() const;
    const char* reg32Name() const;

    u8 m_op { 0 };
    u8 m_subOp { 0 };
    u32 m_imm1 { 0 };
    u32 m_imm2 { 0 };
    u8 m_registerIndex { 0 };
    bool m_a32 { false };
    bool m_o32 { false };
    bool m_hasLockPrefix { false };

    bool m_hasSubOp { false };
    bool m_hasRM { false };

    unsigned m_imm1Bytes { 0 };
    unsigned m_imm2Bytes { 0 };
    unsigned m_prefixBytes { 0 };

    SegmentRegisterIndex m_segmentPrefix { SegmentRegisterIndex::None };
    bool m_hasOperandSizeOverridePrefix { false };
    bool m_hasAddressSizeOverridePrefix { false };
    u8 m_repPrefix { 0 };

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

void buildOpcodeTablesIfNeeded();
