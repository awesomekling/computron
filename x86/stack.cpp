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

void CPU::push_segment_register_value(u16 value)
{
    if (o16()) {
        push16(value);
        return;
    }
    u32 new_esp = current_stack_pointer() - 4;
    if (s16())
        new_esp &= 0xffff;
    write_memory16(SegmentRegisterIndex::SS, new_esp, value);
    adjust_stack_pointer(-4);
    if (UNLIKELY(options.stacklog))
        vlog(LogCPU, "push32: %04x (at esp=%08x, special 16-bit write for segment registers)", value, get_esp());
}

void CPU::push32(u32 value)
{
    u32 new_esp = current_stack_pointer() - 4;
    if (s16())
        new_esp &= 0xffff;
    write_memory32(SegmentRegisterIndex::SS, new_esp, value);
    adjust_stack_pointer(-4);
    if (UNLIKELY(options.stacklog))
        vlog(LogCPU, "push32: %08x (at esp=%08x)", value, current_stack_pointer());
}

void CPU::push16(u16 value)
{
    u32 new_esp = current_stack_pointer() - 2;
    if (s16())
        new_esp &= 0xffff;
    write_memory16(SegmentRegisterIndex::SS, new_esp, value);
    adjust_stack_pointer(-2);
    if (UNLIKELY(options.stacklog))
        vlog(LogCPU, "push16: %04x (at esp=%08x)", value, current_stack_pointer());
}

u32 CPU::pop32()
{
    u32 data = read_memory32(SegmentRegisterIndex::SS, current_stack_pointer());
    if (UNLIKELY(options.stacklog))
        vlog(LogCPU, "pop32: %08x (from esp=%08x)", data, current_stack_pointer());
    adjust_stack_pointer(4);
    return data;
}

u16 CPU::pop16()
{
    u16 data = read_memory16(SegmentRegisterIndex::SS, current_stack_pointer());
    if (UNLIKELY(options.stacklog))
        vlog(LogCPU, "pop16: %04x (from esp=%08x)", data, current_stack_pointer());
    adjust_stack_pointer(2);
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
    push_segment_register_value(get_cs());
}

void CPU::_PUSH_DS(Instruction&)
{
    push_segment_register_value(get_ds());
}

void CPU::_PUSH_ES(Instruction&)
{
    push_segment_register_value(get_es());
}

void CPU::_PUSH_SS(Instruction&)
{
    push_segment_register_value(get_ss());
}

void CPU::_PUSH_FS(Instruction&)
{
    push_segment_register_value(get_fs());
}

void CPU::_PUSH_GS(Instruction&)
{
    push_segment_register_value(get_gs());
}

void CPU::_POP_DS(Instruction&)
{
    set_ds(pop_operand_sized_value());
}

void CPU::_POP_ES(Instruction&)
{
    set_es(pop_operand_sized_value());
}

void CPU::_POP_SS(Instruction&)
{
    set_ss(pop_operand_sized_value());
    make_next_instruction_uninterruptible();
}

void CPU::_POP_FS(Instruction&)
{
    set_fs(pop_operand_sized_value());
}

void CPU::_POP_GS(Instruction&)
{
    set_gs(pop_operand_sized_value());
}

void CPU::_PUSHFD(Instruction&)
{
    if (get_pe() && get_vm() && get_iopl() < 3)
        throw GeneralProtectionFault(0, "PUSHFD in VM86 mode with IOPL < 3");
    push32(get_eflags() & 0x00fcffff);
}

void CPU::_PUSH_imm32(Instruction& insn)
{
    push32(insn.imm32());
}

void CPU::_PUSHF(Instruction&)
{
    if (get_pe() && get_vm() && get_iopl() < 3)
        throw GeneralProtectionFault(0, "PUSHF in VM86 mode with IOPL < 3");
    push16(get_flags());
}

void CPU::_POPF(Instruction&)
{
    if (get_pe() && get_vm() && get_iopl() < 3)
        throw GeneralProtectionFault(0, "POPF in VM86 mode with IOPL < 3");
    set_eflags_respectfully(pop16(), get_cpl());
}

void CPU::_POPFD(Instruction&)
{
    if (get_pe() && get_vm() && get_iopl() < 3)
        throw GeneralProtectionFault(0, "POPFD in VM86 mode with IOPL < 3");
    set_eflags_respectfully(pop32(), get_cpl());
}

void CPU::set_eflags_respectfully(u32 newFlags, u8 effectiveCPL)
{
    u32 oldFlags = get_eflags();
    u32 flagsToKeep = Flag::VIP | Flag::VIF | Flag::RF;
    if (o16())
        flagsToKeep |= 0xffff0000;
    if (get_vm())
        flagsToKeep |= Flag::IOPL;
    if (get_pe() && effectiveCPL != 0) {
        flagsToKeep |= Flag::IOPL;
        if (effectiveCPL > get_iopl()) {
            flagsToKeep |= Flag::IF;
        }
    }
    newFlags &= ~flagsToKeep;
    newFlags |= oldFlags & flagsToKeep;
    newFlags &= ~Flag::RF;
    set_eflags(newFlags);
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
    push<T>(read_register<T>(RegisterBP));
    T frameTemp = read_register<T>(RegisterSP);

    if (nestingLevel > 0) {
        u32 tempBasePointer = current_base_pointer();
        for (u8 i = 1; i < nestingLevel; ++i) {
            tempBasePointer -= sizeof(T);
            push<T>(read_memory<T>(SegmentRegisterIndex::SS, tempBasePointer));
        }
        push<T>(frameTemp);
    }
    write_register<T>(RegisterBP, frameTemp);
    adjust_stack_pointer(-size);
    snoop(SegmentRegisterIndex::SS, current_stack_pointer(), MemoryAccessType::Write);
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
    T newBasePointer = read_memory<T>(SegmentRegisterIndex::SS, current_base_pointer());
    set_current_stack_pointer(current_base_pointer() + sizeof(T));
    if constexpr (sizeof(T) == 2)
        set_bp(newBasePointer);
    else
        set_ebp(newBasePointer);
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
    u32 new_esp = current_stack_pointer() - sizeof(T) * 8;
    if (s16())
        new_esp &= 0xffff;

    snoop(SegmentRegisterIndex::SS, current_stack_pointer(), MemoryAccessType::Write);
    snoop(SegmentRegisterIndex::SS, new_esp, MemoryAccessType::Write);

    T oldStackPointer = read_register<T>(RegisterSP);
    push<T>(read_register<T>(RegisterAX));
    push<T>(read_register<T>(RegisterCX));
    push<T>(read_register<T>(RegisterDX));
    push<T>(read_register<T>(RegisterBX));
    push<T>(oldStackPointer);
    push<T>(read_register<T>(RegisterBP));
    push<T>(read_register<T>(RegisterSI));
    push<T>(read_register<T>(RegisterDI));
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
    u32 new_esp = current_stack_pointer() + sizeof(T) * 8;
    if (s16())
        new_esp &= 0xffff;

    snoop(SegmentRegisterIndex::SS, current_stack_pointer(), MemoryAccessType::Read);
    snoop(SegmentRegisterIndex::SS, new_esp, MemoryAccessType::Read);

    write_register<T>(RegisterDI, pop<T>());
    write_register<T>(RegisterSI, pop<T>());
    write_register<T>(RegisterBP, pop<T>());
    pop<T>();
    write_register<T>(RegisterBX, pop<T>());
    write_register<T>(RegisterDX, pop<T>());
    write_register<T>(RegisterCX, pop<T>());
    write_register<T>(RegisterAX, pop<T>());
}

void CPU::_POPA(Instruction&)
{
    doPOPA<u16>();
}

void CPU::_POPAD(Instruction&)
{
    doPOPA<u32>();
}
