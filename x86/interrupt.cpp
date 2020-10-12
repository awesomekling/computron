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
#include "Common.h"
#include "Tasking.h"
#include "debug.h"
#include "debugger.h"

void CPU::_INT_imm8(Instruction& insn)
{
#ifdef VMM_TRACING
    if (insn.imm8() == 0x20) {
        u16 service_id = readMemory16(SegmentRegisterIndex::CS, getEIP());
        if (service_id < m_vmm_names.size())
            vlog(LogCPU, "VMM %04x: %s", service_id, qPrintable(m_vmm_names.at(service_id)));
        else {
            vlog(LogCPU, "VMM %04x: unknown service", service_id);
        }
    }
#endif
    interrupt(insn.imm8(), InterruptSource::Internal);
}

void CPU::_INT3(Instruction&)
{
    interrupt(3, InterruptSource::Internal);
}

void CPU::_INTO(Instruction&)
{
    /* XXX: I've never seen this used, so it's probably good to log it. */
    vlog(LogAlert, "INTO used, can you believe it?");

    if (get_of())
        interrupt(4, InterruptSource::Internal);
}

void CPU::iret_from_vm86_mode()
{
    if (get_iopl() != 3)
        throw GeneralProtectionFault(0, "IRET in VM86 mode with IOPL != 3");

    u8 originalCPL = get_cpl();

    TransactionalPopper popper(*this);
    u32 offset = popper.pop_operand_sized_value();
    u16 selector = popper.pop_operand_sized_value();
    u32 flags = popper.pop_operand_sized_value();

    if (offset & 0xffff0000)
        throw GeneralProtectionFault(0, "IRET in VM86 mode to EIP > 0xffff");

    set_cs(selector);
    set_eip(offset);
    set_eflags_respectfully(flags, originalCPL);
    popper.commit();
}

void CPU::iret_from_real_mode()
{
    u32 offset = pop_operand_sized_value();
    u16 selector = pop_operand_sized_value();
    u32 flags = pop_operand_sized_value();

#ifdef DEBUG_JUMPS
    vlog(LogCPU, "Popped %u-bit cs:eip:eflags %04x:%08x:%08x @stack{%04x:%08x}", o16() ? 16 : 32, selector, offset, flags, get_ss(), currentStackPointer());
#endif

    set_cs(selector);
    set_eip(offset);

    set_eflags_respectfully(flags, 0);
}

void CPU::_IRET(Instruction&)
{
    if (!get_pe()) {
        iret_from_real_mode();
        return;
    }

    if (get_vm()) {
        iret_from_vm86_mode();
        return;
    }

    u16 originalCPL = get_cpl();

    if (get_nt()) {
        auto tss = current_tss();
#ifdef DEBUG_TASK_SWITCH
        vlog(LogCPU, "IRET with NT=1 switching tasks. Inner TSS @ %08X -> Outer TSS sel %04X...", TR.base, tss.getBacklink());
#endif
        task_switch(tss.get_backlink(), JumpType::IRET);
        return;
    }

    TransactionalPopper popper(*this);

    u32 offset = popper.pop_operand_sized_value();
    u16 selector = popper.pop_operand_sized_value();
    u32 flags = popper.pop_operand_sized_value();
#ifdef DEBUG_JUMPS
    vlog(LogCPU, "Popped %u-bit cs:eip:eflags %04x:%08x:%08x @stack{%04x:%08x}", o16() ? 16 : 32, selector, offset, flags, get_ss(), popper.adjustedStackPointer());
#endif

    if (flags & Flag::VM) {
        if (get_cpl() == 0) {
            iret_to_vm86_mode(popper, LogicalAddress(selector, offset), flags);
            return;
        }
        vlog(LogCPU, "IRET to VM86 but CPL = %u!?", get_cpl());
        ASSERT_NOT_REACHED();
    }
    protected_iret(popper, LogicalAddress(selector, offset));

    set_eflags_respectfully(flags, originalCPL);
}

static u16 makeErrorCode(u16 num, bool idt, CPU::InterruptSource source)
{
    if (idt)
        return (num << 3) | 2 | (u16)source;
    return (num & 0xfc) | (u16)source;
}

void CPU::interrupt_to_task_gate(u8, InterruptSource source, QVariant errorCode, Gate& gate)
{
    auto descriptor = get_descriptor(gate.selector());
    if (options.trapint) {
        dump_descriptor(descriptor);
    }
    if (!descriptor.is_global()) {
        throw GeneralProtectionFault(makeErrorCode(gate.selector(), 0, source), "Interrupt to task gate referencing local descriptor");
    }
    if (!descriptor.is_tss()) {
        throw GeneralProtectionFault(makeErrorCode(gate.selector(), 0, source), "Interrupt to task gate referencing non-TSS descriptor");
    }
    auto& tssDescriptor = descriptor.as_tss_descriptor();
    if (tssDescriptor.is_busy()) {
        throw GeneralProtectionFault(makeErrorCode(gate.selector(), 0, source), "Interrupt to task gate referencing busy TSS descriptor");
    }
    if (!tssDescriptor.present()) {
        throw GeneralProtectionFault(makeErrorCode(gate.selector(), 0, source), "Interrupt to task gate referencing non-present TSS descriptor");
    }
    task_switch(gate.selector(), tssDescriptor, JumpType::INT);
    if (errorCode.isValid()) {
        if (tssDescriptor.is_32bit())
            push32(errorCode.value<u16>());
        else
            push16(errorCode.value<u16>());
    }
}

LogicalAddress CPU::get_real_mode_interrupt_vector(u8 index)
{
    u16 selector = read_physical_memory<u16>(PhysicalAddress(index * 4 + 2));
    u16 offset = read_physical_memory<u16>(PhysicalAddress(index * 4));
    return { selector, offset };
}

void CPU::real_mode_interrupt(u8 isr, InterruptSource source)
{
    ASSERT(!get_pe());
    u16 originalCS = get_cs();
    u16 originalIP = get_ip();
    u16 flags = get_flags();
    auto vector = get_real_mode_interrupt_vector(isr);

    if (options.trapint)
        vlog(LogCPU, "PE=0 interrupt %02x,%04x%s -> %04x:%04x", isr, get_ax(), source == InterruptSource::External ? " (external)" : "", vector.selector(), vector.offset());

#ifdef LOG_FAR_JUMPS
    vlog(LogCPU, "[PE=0] Interrupt from %04x:%08x to %04x:%08x", get_base_cs(), current_base_instruction_pointer(), vector.selector(), vector.offset());
#endif

    set_cs(vector.selector());
    set_eip(vector.offset());

    push16(flags);
    push16(originalCS);
    push16(originalIP);

    set_if(0);
    set_tf(0);
}

#ifdef DEBUG_SERENITY
#    include "../serenity/Kernel/API/Syscall.h"

static void logSerenitySyscall(CPU& cpu)
{
    auto func = (Syscall::Function)cpu.get_eax();
    vlog(LogSerenity, "Syscall %02u %s (%08x, %08x, %08x)", cpu.get_eax(), Syscall::to_string(func), cpu.get_edx(), cpu.get_ecx(), cpu.get_ebx());
}
#endif

static const int ignoredInterrupt = -1;

void CPU::protected_mode_interrupt(u8 isr, InterruptSource source, QVariant errorCode)
{
    ASSERT(get_pe());

#if DEBUG_SERENITY
    bool logAsSyscall = options.trapint && options.serenity && isr == 0x80;

    if (logAsSyscall)
        logSerenitySyscall(*this);
#else
    bool logAsSyscall = false;
#endif

    if (source == InterruptSource::Internal && get_vm() && get_iopl() != 3) {
        throw GeneralProtectionFault(0, "Software INT in VM86 mode with IOPL != 3");
    }

    auto idtEntry = get_interrupt_descriptor(isr);
    if (!idtEntry.is_task_gate() && !idtEntry.is_trap_gate() && !idtEntry.is_interrupt_gate()) {
        throw GeneralProtectionFault(makeErrorCode(isr, 1, source), "Interrupt to invalid gate type");
    }
    auto& gate = idtEntry.as_gate();

    if (source == InterruptSource::Internal) {
        if (gate.dpl() < get_cpl()) {
            throw GeneralProtectionFault(makeErrorCode(isr, 1, source), QString("Software interrupt trying to escalate privilege (CPL=%1, DPL=%2, VM=%3)").arg(get_cpl()).arg(gate.dpl()).arg(get_vm()));
        }
    }

    if (!gate.present()) {
        throw NotPresent(makeErrorCode(isr, 1, source), "Interrupt gate not present");
    }

    if (gate.is_null()) {
        throw GeneralProtectionFault(makeErrorCode(isr, 1, source), "Interrupt gate is null");
    }

    auto entry = gate.entry();

    if (options.trapint && !logAsSyscall && isr != ignoredInterrupt) {
        vlog(LogCPU, "PE=1 interrupt %02x,%04x%s, type: %s (%1x), %04x:%08x", isr, get_ax(), source == InterruptSource::External ? " (external)" : "", gate.type_name(), gate.type(), entry.selector(), entry.offset());
        dump_descriptor(gate);
    }

    if (gate.is_task_gate()) {
        interrupt_to_task_gate(isr, source, errorCode, gate);
        return;
    }

    auto descriptor = get_descriptor(gate.selector());

    if (options.trapint && !logAsSyscall && isr != ignoredInterrupt) {
        dump_descriptor(descriptor);
    }

    if (descriptor.is_null()) {
        throw GeneralProtectionFault(source == InterruptSource::External, "Interrupt gate to null descriptor");
    }

    if (descriptor.is_outside_table_limits()) {
        throw GeneralProtectionFault(makeErrorCode(gate.selector(), 0, source), "Interrupt gate to descriptor outside table limit");
    }

    if (!descriptor.is_code()) {
        dump_descriptor(descriptor);
        throw GeneralProtectionFault(makeErrorCode(gate.selector(), 0, source), "Interrupt gate to non-code segment");
    }

    auto& codeDescriptor = descriptor.as_code_segment_descriptor();
    if (codeDescriptor.dpl() > get_cpl()) {
        throw GeneralProtectionFault(makeErrorCode(gate.selector(), 0, source), QString("Interrupt gate to segment with DPL(%1)>CPL(%2)").arg(codeDescriptor.dpl()).arg(get_cpl()));
    }

    if (!codeDescriptor.present()) {
        throw NotPresent(makeErrorCode(gate.selector(), 0, source), "Interrupt to non-present segment");
    }

    u32 offset = gate.offset();
    u32 flags = get_eflags();

    u16 originalSS = get_ss();
    u32 originalESP = get_esp();
    u16 originalCPL = get_cpl();
    u16 originalCS = get_cs();
    u32 originalEIP = get_eip();

    if (!gate.is_32bit() || !codeDescriptor.is_32bit()) {
        if (offset & 0xffff0000) {
            vlog(LogCPU, "Truncating interrupt entry offset from %04x:%08x to %04x:%08x", gate.selector(), offset, gate.selector(), offset & 0xffff);
        }
        offset &= 0xffff;
    }

    // FIXME: Stack-related exceptions should come before this.
    if (offset > codeDescriptor.effective_limit()) {
        throw GeneralProtectionFault(0, "Offset outside segment limit");
    }

    if (get_vm()) {
        interrupt_from_vm86_mode(gate, offset, codeDescriptor, source, errorCode);
        return;
    }

    if (!codeDescriptor.conforming() && descriptor.dpl() < originalCPL) {
#ifdef DEBUG_JUMPS
        vlog(LogCPU, "Interrupt escalating privilege from ring%u to ring%u", originalCPL, descriptor.DPL(), descriptor);
#endif
        auto tss = current_tss();

        u16 newSS = tss.get_ring_ss(descriptor.dpl());
        u32 newESP = tss.get_ring_esp(descriptor.dpl());
        auto newSSDescriptor = get_descriptor(newSS);

        if (newSSDescriptor.is_null()) {
            throw InvalidTSS(source == InterruptSource::External, "New ss is null");
        }

        if (newSSDescriptor.is_outside_table_limits()) {
            throw InvalidTSS(makeErrorCode(newSS, 0, source), "New ss outside table limits");
        }

        if (newSSDescriptor.dpl() != descriptor.dpl()) {
            throw InvalidTSS(makeErrorCode(newSS, 0, source), QString("New ss DPL(%1) != code segment DPL(%2)").arg(newSSDescriptor.dpl()).arg(descriptor.dpl()));
        }

        if (!newSSDescriptor.is_data() || !newSSDescriptor.as_data_segment_descriptor().writable()) {
            throw InvalidTSS(makeErrorCode(newSS, 0, source), "New ss not a writable data segment");
        }

        if (!newSSDescriptor.present()) {
            throw StackFault(makeErrorCode(newSS, 0, source), "New ss not present");
        }

        BEGIN_ASSERT_NO_EXCEPTIONS
        set_cpl(descriptor.dpl());
        set_ss(newSS);
        set_esp(newESP);

#ifdef DEBUG_JUMPS
        vlog(LogCPU, "Interrupt to inner ring, ss:esp %04x:%08x -> %04x:%08x", originalSS, originalESP, get_ss(), get_esp());
        vlog(LogCPU, "Push %u-bit ss:esp %04x:%08x @stack{%04x:%08x}", gate.size(), originalSS, originalESP, get_ss(), get_esp());
#endif
        push_value_with_size(originalSS, gate.size());
        push_value_with_size(originalESP, gate.size());
        END_ASSERT_NO_EXCEPTIONS
    } else if (codeDescriptor.conforming() || codeDescriptor.dpl() == originalCPL) {
#ifdef DEBUG_JUMPS
        vlog(LogCPU, "Interrupt same privilege from ring%u to ring%u", originalCPL, descriptor.DPL());
#endif
        if (get_vm() && (codeDescriptor.conforming() || codeDescriptor.dpl() != 0)) {
            ASSERT_NOT_REACHED();
            throw GeneralProtectionFault(gate.selector() & ~3, "Interrupt in VM86 mode to code segment with DPL != 0");
        }

        set_cpl(originalCPL);
    } else {
        ASSERT_NOT_REACHED();
        throw GeneralProtectionFault(makeErrorCode(gate.selector(), 0, source), "Interrupt to non-conforming code segment with DPL > CPL");
    }

#ifdef DEBUG_JUMPS
    vlog(LogCPU, "Push %u-bit flags %08x @stack{%04x:%08x}", gate.size(), flags, get_ss(), get_esp());
    vlog(LogCPU, "Push %u-bit cs:eip %04x:%08x @stack{%04x:%08x}", gate.size(), originalCS, originalEIP, get_ss(), get_esp());
#endif
    BEGIN_ASSERT_NO_EXCEPTIONS
    push_value_with_size(flags, gate.size());
    push_value_with_size(originalCS, gate.size());
    push_value_with_size(originalEIP, gate.size());
    if (errorCode.isValid()) {
        push_value_with_size(errorCode.value<u16>(), gate.size());
    }

    if (gate.is_interrupt_gate())
        set_if(0);
    set_tf(0);
    set_rf(0);
    set_nt(0);
    set_vm(0);
    set_cs(gate.selector());
    set_eip(offset);
    END_ASSERT_NO_EXCEPTIONS
}

void CPU::interrupt_from_vm86_mode(Gate& gate, u32 offset, CodeSegmentDescriptor& codeDescriptor, InterruptSource source, QVariant errorCode)
{
#ifdef DEBUG_VM86
    vlog(LogCPU, "Interrupt from VM86 mode -> %04x:%08x", gate.selector(), offset);
#endif

    u32 originalFlags = get_eflags();
    u16 originalSS = get_ss();
    u32 originalESP = get_esp();

    if (codeDescriptor.dpl() != 0) {
        throw GeneralProtectionFault(makeErrorCode(gate.selector(), 0, source), "Interrupt from VM86 mode to descriptor with CPL != 0");
    }

    auto tss = current_tss();

    u16 newSS = tss.get_ss0();
    u32 newESP = tss.get_esp0();
    auto newSSDescriptor = get_descriptor(newSS);

    if (newSSDescriptor.is_null()) {
        throw InvalidTSS(source == InterruptSource::External, "New ss is null");
    }

    if (newSSDescriptor.is_outside_table_limits()) {
        throw InvalidTSS(makeErrorCode(newSS, 0, source), "New ss outside table limits");
    }

    if ((newSS & 3) != 0) {
        throw InvalidTSS(makeErrorCode(newSS, 0, source), QString("New ss RPL(%1) != 0").arg(newSS & 3));
    }

    if (newSSDescriptor.dpl() != 0) {
        throw InvalidTSS(makeErrorCode(newSS, 0, source), QString("New ss DPL(%1) != 0").arg(newSSDescriptor.dpl()));
    }

    if (!newSSDescriptor.is_data() || !newSSDescriptor.as_data_segment_descriptor().writable()) {
        throw InvalidTSS(makeErrorCode(newSS, 0, source), "New ss not a writable data segment");
    }

    if (!newSSDescriptor.present()) {
        throw StackFault(makeErrorCode(newSS, 0, source), "New ss not present");
    }

#ifdef DEBUG_VM86
    vlog(LogCPU, "VM86 ss:esp %04x:%08x -> %04x:%08x", originalSS, originalESP, newSS, newESP);
#endif

    BEGIN_ASSERT_NO_EXCEPTIONS
    set_cpl(0);
    set_vm(0);
    set_tf(0);
    set_rf(0);
    set_nt(0);
    if (gate.is_interrupt_gate())
        set_if(0);
    set_ss(newSS);
    set_esp(newESP);
    push_value_with_size(get_gs(), gate.size());
    push_value_with_size(get_fs(), gate.size());
    push_value_with_size(get_ds(), gate.size());
    push_value_with_size(get_es(), gate.size());
#ifdef DEBUG_VM86
    vlog(LogCPU, "Push %u-bit ss:esp %04x:%08x @stack{%04x:%08x}", gate.size(), originalSS, originalESP, get_ss(), get_esp());
    LinearAddress esp_laddr = cached_descriptor(SegmentRegisterIndex::SS).base().offset(get_esp());
    PhysicalAddress esp_paddr = translateAddress(esp_laddr, MemoryAccessType::Write);
    vlog(LogCPU, "Relevant stack pointer at P 0x%08x", esp_paddr.get());
#endif
    push_value_with_size(originalSS, gate.size());
    push_value_with_size(originalESP, gate.size());
#ifdef DEBUG_VM86
    vlog(LogCPU, "Pushing original flags %08x (VM=%u)", originalFlags, !!(originalFlags & Flag::VM));
#endif
    push_value_with_size(originalFlags, gate.size());
    push_value_with_size(get_cs(), gate.size());
    push_value_with_size(get_eip(), gate.size());
    if (errorCode.isValid()) {
        push_value_with_size(errorCode.value<u16>(), gate.size());
    }
    set_gs(0);
    set_fs(0);
    set_ds(0);
    set_es(0);
    set_cs(gate.selector());
    set_cpl(0);
    set_eip(offset);
    END_ASSERT_NO_EXCEPTIONS
}

void CPU::interrupt(u8 isr, InterruptSource source, QVariant errorCode)
{
    if (get_pe())
        protected_mode_interrupt(isr, source, errorCode);
    else
        real_mode_interrupt(isr, source);
}

void CPU::protected_iret(TransactionalPopper& popper, LogicalAddress address)
{
    ASSERT(get_pe());
#ifdef DEBUG_JUMPS
    u16 originalSS = get_ss();
    u32 originalESP = get_esp();
    u16 originalCS = getCS();
    u32 originalEIP = getEIP();
#endif

    u16 selector = address.selector();
    u32 offset = address.offset();
    u16 originalCPL = get_cpl();
    u8 selectorRPL = selector & 3;

#ifdef LOG_FAR_JUMPS
    vlog(LogCPU, "[PE=%u, PG=%u] IRET from %04x:%08x to %04x:%08x", get_pe(), getPG(), get_base_cs(), current_base_instruction_pointer(), selector, offset);
#endif

    auto descriptor = get_descriptor(selector);

    if (descriptor.is_null())
        throw GeneralProtectionFault(0, "IRET to null selector");

    if (descriptor.is_outside_table_limits())
        throw GeneralProtectionFault(selector & 0xfffc, "IRET to selector outside table limit");

    if (!descriptor.is_code()) {
        dump_descriptor(descriptor);
        throw GeneralProtectionFault(selector & 0xfffc, "Not a code segment");
    }

    if (selectorRPL < get_cpl())
        throw GeneralProtectionFault(selector & 0xfffc, QString("IRET with RPL(%1) < CPL(%2)").arg(selectorRPL).arg(get_cpl()));

    auto& codeSegment = descriptor.as_code_segment_descriptor();

    if (codeSegment.conforming() && codeSegment.dpl() > selectorRPL)
        throw GeneralProtectionFault(selector & 0xfffc, "IRET to conforming code segment with DPL > RPL");

    if (!codeSegment.conforming() && codeSegment.dpl() != selectorRPL)
        throw GeneralProtectionFault(selector & 0xfffc, "IRET to non-conforming code segment with DPL != RPL");

    if (!codeSegment.present())
        throw NotPresent(selector & 0xfffc, "Code segment not present");

    // NOTE: A 32-bit jump into a 16-bit segment might have irrelevant higher bits set.
    // Mask them off to make sure we don't incorrectly fail limit checks.
    if (!codeSegment.is_32bit())
        offset &= 0xffff;

    if (offset > codeSegment.effective_limit()) {
        vlog(LogCPU, "IRET to eip(%08x) outside limit(%08x)", offset, codeSegment.effective_limit());
        dump_descriptor(codeSegment);
        throw GeneralProtectionFault(0, "Offset outside segment limit");
    }

    u16 newSS;
    u32 newESP;
    if (selectorRPL > originalCPL) {
        BEGIN_ASSERT_NO_EXCEPTIONS
        newESP = popper.pop_operand_sized_value();
        newSS = popper.pop_operand_sized_value();
#ifdef DEBUG_JUMPS
        vlog(LogCPU, "Popped %u-bit ss:esp %04x:%08x @stack{%04x:%08x}", o16() ? 16 : 32, newSS, newESP, get_ss(), popper.adjustedStackPointer());
        vlog(LogCPU, "IRET from ring%u to ring%u, ss:esp %04x:%08x -> %04x:%08x", originalCPL, getCPL(), originalSS, originalESP, newSS, newESP);
#endif
        END_ASSERT_NO_EXCEPTIONS
    }

    // FIXME: Validate SS before clobbering CS:EIP.
    set_cs(selector);
    set_eip(offset);

    if (selectorRPL > originalCPL) {
        set_ss(newSS);
        set_esp(newESP);

        clear_segment_register_after_return_if_needed(SegmentRegisterIndex::ES, JumpType::IRET);
        clear_segment_register_after_return_if_needed(SegmentRegisterIndex::FS, JumpType::IRET);
        clear_segment_register_after_return_if_needed(SegmentRegisterIndex::GS, JumpType::IRET);
        clear_segment_register_after_return_if_needed(SegmentRegisterIndex::DS, JumpType::IRET);
    } else {
        popper.commit();
    }
}

void CPU::iret_to_vm86_mode(TransactionalPopper& popper, LogicalAddress entry, u32 flags)
{
#ifdef DEBUG_VM86
    vlog(LogCPU, "IRET (o%u) to VM86 mode -> %04x:%04x", o16() ? 16 : 32, entry.selector(), entry.offset());
#endif
    if (!o32()) {
        vlog(LogCPU, "Hmm, o16 IRET to VM86!?");
        ASSERT_NOT_REACHED();
    }

    if (entry.offset() & 0xffff0000)
        throw GeneralProtectionFault(0, "IRET to VM86 with offset > 0xffff");

    set_eflags(flags);
    set_cs(entry.selector());
    set_eip(entry.offset());

    u32 newESP = popper.pop32();
    u16 newSS = popper.pop32();
    set_es(popper.pop32());
    set_ds(popper.pop32());
    set_fs(popper.pop32());
    set_gs(popper.pop32());
    set_cpl(3);
    set_esp(newESP);
    set_ss(newSS);
}
