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
#include "debug.h"

void CPU::pushSegmentRegisterValue(u16 value)
{
    if (o16()) {
        push16(value);
        return;
    }
    u32 new_esp = currentStackPointer() - 4;
    if (s16())
        new_esp &= 0xffff;
    writeMemory16(SegmentRegisterIndex::SS, new_esp, value);
    adjustStackPointer(-4);
    if (UNLIKELY(options.stacklog))
        vlog(LogCPU, "push32: %04x (at esp=%08x, special 16-bit write for segment registers)", value, getESP());
}

void CPU::push32(u32 value)
{
    u32 new_esp = currentStackPointer() - 4;
    if (s16())
        new_esp &= 0xffff;
    writeMemory32(SegmentRegisterIndex::SS, new_esp, value);
    adjustStackPointer(-4);
    if (UNLIKELY(options.stacklog))
        vlog(LogCPU, "push32: %08x (at esp=%08x)", value, currentStackPointer());
}

void CPU::push16(u16 value)
{
    u32 new_esp = currentStackPointer() - 2;
    if (s16())
        new_esp &= 0xffff;
    writeMemory16(SegmentRegisterIndex::SS, new_esp, value);
    adjustStackPointer(-2);
    if (UNLIKELY(options.stacklog))
        vlog(LogCPU, "push16: %04x (at esp=%08x)", value, currentStackPointer());
}

u32 CPU::pop32()
{
    u32 data = readMemory32(SegmentRegisterIndex::SS, currentStackPointer());
    if (UNLIKELY(options.stacklog))
        vlog(LogCPU, "pop32: %08x (from esp=%08x)", data, currentStackPointer());
    adjustStackPointer(4);
    return data;
}

u16 CPU::pop16()
{
    u16 data = readMemory16(SegmentRegisterIndex::SS, currentStackPointer());
    if (UNLIKELY(options.stacklog))
        vlog(LogCPU, "pop16: %04x (from esp=%08x)", data, currentStackPointer());
    adjustStackPointer(2);
    return data;
}

void CPU::_PUSH_reg16(Instruction& insn)
{
    push16(insn.reg16());
}

void CPU::_PUSH_reg32(Instruction& insn)
{
    push32(insn.reg32());
}

void CPU::_POP_reg16(Instruction& insn)
{
    insn.reg16() = pop16();
}

void CPU::_POP_reg32(Instruction& insn)
{
    insn.reg32() = pop32();
}

void CPU::_PUSH_RM16(Instruction& insn)
{
    push16(insn.modrm().read16());
}

void CPU::_PUSH_RM32(Instruction& insn)
{
    push32(insn.modrm().read32());
}

// From the IA-32 manual:
// "If the ESP register is used as a base register for addressing a destination operand in memory,
// the POP instruction computes the effective address of the operand after it increments the ESP register."

void CPU::_POP_RM16(Instruction& insn)
{
    // See comment above.
    auto data = pop16();
    insn.modrm().resolve(*this);
    insn.modrm().write16(data);
}

void CPU::_POP_RM32(Instruction& insn)
{
    // See comment above.
    auto data = pop32();
    insn.modrm().resolve(*this);
    insn.modrm().write32(data);
}

void CPU::_PUSH_CS(Instruction&)
{
    pushSegmentRegisterValue(getCS());
}

void CPU::_PUSH_DS(Instruction&)
{
    pushSegmentRegisterValue(getDS());
}

void CPU::_PUSH_ES(Instruction&)
{
    pushSegmentRegisterValue(getES());
}

void CPU::_PUSH_SS(Instruction&)
{
    pushSegmentRegisterValue(getSS());
}

void CPU::_PUSH_FS(Instruction&)
{
    pushSegmentRegisterValue(getFS());
}

void CPU::_PUSH_GS(Instruction&)
{
    pushSegmentRegisterValue(getGS());
}

void CPU::_POP_DS(Instruction&)
{
    setDS(popOperandSizedValue());
}

void CPU::_POP_ES(Instruction&)
{
    setES(popOperandSizedValue());
}

void CPU::_POP_SS(Instruction&)
{
    setSS(popOperandSizedValue());
    makeNextInstructionUninterruptible();
}

void CPU::_POP_FS(Instruction&)
{
    setFS(popOperandSizedValue());
}

void CPU::_POP_GS(Instruction&)
{
    setGS(popOperandSizedValue());
}

void CPU::_PUSHFD(Instruction&)
{
    if (getPE() && getVM() && getIOPL() < 3)
        throw GeneralProtectionFault(0, "PUSHFD in VM86 mode with IOPL < 3");
    push32(getEFlags() & 0x00fcffff);
}

void CPU::_PUSH_imm32(Instruction& insn)
{
    push32(insn.imm32());
}

void CPU::_PUSHF(Instruction&)
{
    if (getPE() && getVM() && getIOPL() < 3)
        throw GeneralProtectionFault(0, "PUSHF in VM86 mode with IOPL < 3");
    push16(getFlags());
}

void CPU::_POPF(Instruction&)
{
    if (getPE() && getVM() && getIOPL() < 3)
        throw GeneralProtectionFault(0, "POPF in VM86 mode with IOPL < 3");
    setEFlagsRespectfully(pop16(), getCPL());
}

void CPU::_POPFD(Instruction&)
{
    if (getPE() && getVM() && getIOPL() < 3)
        throw GeneralProtectionFault(0, "POPFD in VM86 mode with IOPL < 3");
    setEFlagsRespectfully(pop32(), getCPL());
}

void CPU::setEFlagsRespectfully(u32 newFlags, u8 effectiveCPL)
{
    u32 oldFlags = getEFlags();
    u32 flagsToKeep = Flag::VIP | Flag::VIF | Flag::RF;
    if (o16())
        flagsToKeep |= 0xffff0000;
    if (getVM())
        flagsToKeep |= Flag::IOPL;
    if (getPE() && effectiveCPL != 0) {
        flagsToKeep |= Flag::IOPL;
        if (effectiveCPL > getIOPL()) {
            flagsToKeep |= Flag::IF;
        }
    }
    newFlags &= ~flagsToKeep;
    newFlags |= oldFlags & flagsToKeep;
    newFlags &= ~Flag::RF;
    setEFlags(newFlags);
}

void CPU::_PUSH_imm8(Instruction& insn)
{
    if (o32())
        push32(signExtendedTo<u32>(insn.imm8()));
    else
        push16(signExtendedTo<u16>(insn.imm8()));
}

void CPU::_PUSH_imm16(Instruction& insn)
{
    push16(insn.imm16());
}

template<typename T>
void CPU::doENTER(Instruction& insn)
{
    u16 size = insn.imm16_2();
    u8 nestingLevel = insn.imm8_1() & 31;
    push<T>(readRegister<T>(RegisterBP));
    T frameTemp = readRegister<T>(RegisterSP);

    if (nestingLevel > 0) {
        u32 tempBasePointer = currentBasePointer();
        for (u8 i = 1; i < nestingLevel; ++i) {
            tempBasePointer -= sizeof(T);
            push<T>(readMemory<T>(SegmentRegisterIndex::SS, tempBasePointer));
        }
        push<T>(frameTemp);
    }
    writeRegister<T>(RegisterBP, frameTemp);
    adjustStackPointer(-size);
    snoop(SegmentRegisterIndex::SS, currentStackPointer(), MemoryAccessType::Write);
}

void CPU::_ENTER16(Instruction& insn)
{
    doENTER<u16>(insn);
}

void CPU::_ENTER32(Instruction& insn)
{
    doENTER<u32>(insn);
}

template<typename T>
void CPU::doLEAVE()
{
    T newBasePointer = readMemory<T>(SegmentRegisterIndex::SS, currentBasePointer());
    setCurrentStackPointer(currentBasePointer() + sizeof(T));
    if constexpr (sizeof(T) == 2)
        setBP(newBasePointer);
    else
        setEBP(newBasePointer);
}

void CPU::_LEAVE16(Instruction&)
{
    doLEAVE<u16>();
}

void CPU::_LEAVE32(Instruction&)
{
    doLEAVE<u32>();
}

template<typename T>
void CPU::doPUSHA()
{
    u32 new_esp = currentStackPointer() - sizeof(T) * 8;
    if (s16())
        new_esp &= 0xffff;

    snoop(SegmentRegisterIndex::SS, currentStackPointer(), MemoryAccessType::Write);
    snoop(SegmentRegisterIndex::SS, new_esp, MemoryAccessType::Write);

    T oldStackPointer = readRegister<T>(RegisterSP);
    push<T>(readRegister<T>(RegisterAX));
    push<T>(readRegister<T>(RegisterCX));
    push<T>(readRegister<T>(RegisterDX));
    push<T>(readRegister<T>(RegisterBX));
    push<T>(oldStackPointer);
    push<T>(readRegister<T>(RegisterBP));
    push<T>(readRegister<T>(RegisterSI));
    push<T>(readRegister<T>(RegisterDI));
}

void CPU::_PUSHA(Instruction&)
{
    doPUSHA<u16>();
}

void CPU::_PUSHAD(Instruction&)
{
    doPUSHA<u32>();
}

template<typename T>
void CPU::doPOPA()
{
    u32 new_esp = currentStackPointer() + sizeof(T) * 8;
    if (s16())
        new_esp &= 0xffff;

    snoop(SegmentRegisterIndex::SS, currentStackPointer(), MemoryAccessType::Read);
    snoop(SegmentRegisterIndex::SS, new_esp, MemoryAccessType::Read);

    writeRegister<T>(RegisterDI, pop<T>());
    writeRegister<T>(RegisterSI, pop<T>());
    writeRegister<T>(RegisterBP, pop<T>());
    pop<T>();
    writeRegister<T>(RegisterBX, pop<T>());
    writeRegister<T>(RegisterDX, pop<T>());
    writeRegister<T>(RegisterCX, pop<T>());
    writeRegister<T>(RegisterAX, pop<T>());
}

void CPU::_POPA(Instruction&)
{
    doPOPA<u16>();
}

void CPU::_POPAD(Instruction&)
{
    doPOPA<u32>();
}
