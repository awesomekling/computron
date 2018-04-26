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

#include "debug.h"
#include "types.h"
#include "Common.h"
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
};
};

class InstructionStream {
public:
    virtual BYTE readInstruction8() = 0;
    virtual WORD readInstruction16() = 0;
    virtual DWORD readInstruction32() = 0;
    DWORD readBytes(unsigned count);
};

class SimpleInstructionStream final : public InstructionStream {
public:
    SimpleInstructionStream(const BYTE* data)
        : m_data(data)
    { }

    virtual BYTE readInstruction8() override { return *(m_data++); }
    virtual WORD readInstruction16() override;
    virtual DWORD readInstruction32() override;

private:
    const BYTE* m_data { nullptr };
};

template<typename T>
class RegisterAccessor {
public:
    RegisterAccessor(T& reg) : m_reg(reg) { }
    T get() const { return m_reg; }
    void set(T value) { m_reg = value; }

private:
    T& m_reg;
};

class MemoryOrRegisterReference {
    friend class CPU;
    friend class Instruction;
public:
    template<typename T> T read();
    template<typename T> void write(T);

    BYTE read8();
    WORD read16();
    DWORD read32();
    void write8(BYTE);
    void write16(WORD);
    void write32(DWORD);
    void writeClearing16(WORD, bool o32);

    template<typename T> class Accessor;
    Accessor<BYTE> accessor8();
    Accessor<WORD> accessor16();
    Accessor<DWORD> accessor32();

    QString toStringO8() const;
    QString toStringO16() const;
    QString toStringO32() const;

    bool isRegister() const { return m_registerIndex != 0xffffffff; }
    SegmentRegisterIndex segment() const { ASSERT(!isRegister()); return m_segment; }
    DWORD offset();

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

    DWORD evaluateSIB();

    unsigned m_registerIndex { 0xffffffff };
    SegmentRegisterIndex m_segment { SegmentRegisterIndex::None };
    union {
        DWORD m_offset32 { 0 };
        WORD m_offset16;
    };

    BYTE m_a32 { false };

    BYTE m_rm { 0 };
    BYTE m_sib { 0 };
    BYTE m_displacementBytes { 0 };

    union {
        DWORD m_displacement32 { 0 };
        WORD m_displacement16;
    };

    bool m_hasSIB { false };

    CPU* m_cpu { nullptr };
};

class Instruction {
public:
    static Instruction fromStream(InstructionStream&, bool o32, bool a32);
    ~Instruction() { }

    void execute(CPU&);

    MemoryOrRegisterReference& modrm() { ASSERT(hasRM()); return m_modrm; }

    bool hasSegmentPrefix() const { return m_segmentPrefix != SegmentRegisterIndex::None; }
    bool hasAddressSizeOverridePrefix() const { return m_hasAddressSizeOverridePrefix; }
    bool hasOperandSizeOverridePrefix() const { return m_hasOperandSizeOverridePrefix; }
    bool hasRepPrefix() const { return m_repPrefix; }

    BYTE repPrefix() const { return m_repPrefix; }

    bool isValid() const { return m_descriptor; }

    unsigned length() const;

    QString mnemonic() const;

    BYTE op() const { return m_op; }
    BYTE subOp() const { return m_subOp; }
    BYTE rm() const { return m_modrm.m_rm; }
    BYTE slash() const { ASSERT(hasRM()); return (rm() >> 3) & 7; }

    BYTE imm8() const { ASSERT(m_imm1Bytes == 1); return m_imm1; }
    WORD imm16() const { ASSERT(m_imm1Bytes == 2); return m_imm1; }
    DWORD imm32() const { ASSERT(m_imm1Bytes == 4); return m_imm1; }

    BYTE imm8_1() const { return imm8(); }
    BYTE imm8_2() const { ASSERT(m_imm2Bytes == 1); return m_imm2; }
    WORD imm16_1() const { return imm16(); }
    WORD imm16_2() const { ASSERT(m_imm2Bytes == 2); return m_imm2; }
    DWORD imm32_1() const { return imm32(); }
    DWORD imm32_2() const { ASSERT(m_imm2Bytes == 4); return m_imm2; }

    DWORD immAddress() const { return m_a32 ? imm32() : imm16(); }

    LogicalAddress immAddress16_16() const { return LogicalAddress(imm16_1(), imm16_2()); }
    LogicalAddress immAddress16_32() const { return LogicalAddress(imm16_1(), imm32_2()); }

    // These functions assume that the Instruction is bound to a CPU.
    BYTE& reg8();
    WORD& reg16();
    DWORD& reg32();
    WORD& segreg();

    template<typename T> T& reg();

    bool hasRM() const { return m_hasRM; }
    bool hasSubOp() const { return m_hasSubOp; }

    unsigned registerIndex() const { return m_registerIndex; }
    SegmentRegisterIndex segmentRegisterIndex() const { return static_cast<SegmentRegisterIndex>(registerIndex()); }

    BYTE cc() const { return m_hasSubOp ? m_subOp & 0xf : m_op & 0xf; }

    QString toString(DWORD origin, bool x32) const;

private:
    Instruction(InstructionStream&, bool o32, bool a32);

    QString toStringInternal(DWORD origin, bool x32) const;

    const char* reg8Name() const;
    const char* reg16Name() const;
    const char* reg32Name() const;

    BYTE m_op { 0 };
    BYTE m_subOp { 0 };
    DWORD m_imm1 { 0 };
    DWORD m_imm2 { 0 };
    BYTE m_registerIndex { 0 };
    bool m_a32 { false };
    bool m_o32 { false };

    bool m_hasSubOp { false };
    bool m_hasRM { false };

    unsigned m_imm1Bytes { 0 };
    unsigned m_imm2Bytes { 0 };
    unsigned m_prefixBytes { 0 };

    SegmentRegisterIndex m_segmentPrefix { SegmentRegisterIndex::None };
    bool m_hasOperandSizeOverridePrefix { false };
    bool m_hasAddressSizeOverridePrefix { false };
    BYTE m_repPrefix { 0 };

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
    explicit Accessor(MemoryOrRegisterReference& modrm) : m_modrm(modrm) { }
    MemoryOrRegisterReference& m_modrm;
};

inline MemoryOrRegisterReference::Accessor<BYTE> MemoryOrRegisterReference::accessor8() { return Accessor<BYTE>(*this); }
inline MemoryOrRegisterReference::Accessor<WORD> MemoryOrRegisterReference::accessor16() { return Accessor<WORD>(*this); }
inline MemoryOrRegisterReference::Accessor<DWORD> MemoryOrRegisterReference::accessor32() { return Accessor<DWORD>(*this); }

void buildOpcodeTablesIfNeeded();
