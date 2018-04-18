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

#include "Common.h"
#include "CPU.h"
#include "debug.h"
#include "debugger.h"
#include "Tasking.h"

void CPU::_INT_imm8(Instruction& insn)
{
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

    if (getOF())
        interrupt(4, InterruptSource::Internal);
}

void CPU::iretFromVM86Mode()
{
    if (getIOPL() != 3)
        throw GeneralProtectionFault(0, "IRET in VM86 mode with IOPL != 3");

    BYTE originalCPL = getCPL();

    TransactionalPopper popper(*this);
    DWORD offset = popper.popOperandSizedValue();
    WORD selector = popper.popOperandSizedValue();
    DWORD flags = popper.popOperandSizedValue();

    if (offset & 0xffff0000)
        throw GeneralProtectionFault(0, "IRET in VM86 mode to EIP > 0xffff");

    setCS(selector);
    setEIP(offset);
    setEFlagsRespectfully(flags, originalCPL);
    popper.commit();
}

void CPU::iretFromRealMode()
{
    DWORD offset = popOperandSizedValue();
    WORD selector = popOperandSizedValue();
    DWORD flags = popOperandSizedValue();

#ifdef DEBUG_JUMPS
    vlog(LogCPU, "Popped %u-bit cs:eip:eflags %04x:%08x:%08x @stack{%04x:%08x}", o16() ? 16 : 32, selector, offset, flags, getSS(), currentStackPointer());
#endif

    setCS(selector);
    setEIP(offset);

    setEFlagsRespectfully(flags, 0);
}

void CPU::_IRET(Instruction&)
{
    if (!getPE()) {
        iretFromRealMode();
        return;
    }

    if (getVM()) {
        iretFromVM86Mode();
        return;
    }

    WORD originalCPL = getCPL();

    if (getNT()) {
        auto tss = currentTSS();
#ifdef DEBUG_TASK_SWITCH
        vlog(LogCPU, "IRET with NT=1 switching tasks. Inner TSS @ %08X -> Outer TSS sel %04X...", TR.base, tss.getBacklink());
#endif
        taskSwitch(tss.getBacklink(), JumpType::IRET);
        return;
    }

    TransactionalPopper popper(*this);

    DWORD offset = popper.popOperandSizedValue();
    WORD selector = popper.popOperandSizedValue();
    DWORD flags = popper.popOperandSizedValue();
#ifdef DEBUG_JUMPS
    vlog(LogCPU, "Popped %u-bit cs:eip:eflags %04x:%08x:%08x @stack{%04x:%08x}", o16() ? 16 : 32, selector, offset, flags, getSS(), popper.adjustedStackPointer());
#endif

    if (flags & Flag::VM) {
        if (getCPL() == 0) {
            iretToVM86Mode(popper, LogicalAddress(selector, offset), flags);
            return;
        }
        vlog(LogCPU, "IRET to VM86 but CPL = %u!?", getCPL());
        ASSERT_NOT_REACHED();
    }
    protectedIRET(popper, LogicalAddress(selector, offset));

    setEFlagsRespectfully(flags, originalCPL);
}

static WORD makeErrorCode(WORD num, bool idt, CPU::InterruptSource source)
{
    if (idt)
        return (num << 3) | 2 | (WORD)source;
    return (num & 0xfc) | (WORD)source;
}

void CPU::interruptToTaskGate(BYTE, InterruptSource source, QVariant errorCode, Gate& gate)
{
    auto descriptor = getDescriptor(gate.selector());
    if (options.trapint) {
        dumpDescriptor(descriptor);
    }
    if (!descriptor.isGlobal()) {
        throw GeneralProtectionFault(makeErrorCode(gate.selector(), 0, source), "Interrupt to task gate referencing local descriptor");
    }
    if (!descriptor.isTSS()) {
        throw GeneralProtectionFault(makeErrorCode(gate.selector(), 0, source), "Interrupt to task gate referencing non-TSS descriptor");
    }
    auto& tssDescriptor = descriptor.asTSSDescriptor();
    if (tssDescriptor.isBusy()) {
        throw GeneralProtectionFault(makeErrorCode(gate.selector(), 0, source), "Interrupt to task gate referencing busy TSS descriptor");
    }
    if (!tssDescriptor.present()) {
        throw GeneralProtectionFault(makeErrorCode(gate.selector(), 0, source), "Interrupt to task gate referencing non-present TSS descriptor");
    }
    taskSwitch(tssDescriptor, JumpType::INT);
    if (errorCode.isValid()) {
        if (tssDescriptor.is32Bit())
            push32(errorCode.value<WORD>());
        else
            push16(errorCode.value<WORD>());
    }
}

void CPU::realModeInterrupt(BYTE isr, InterruptSource source)
{
    ASSERT(!getPE());
    WORD originalCS = getCS();
    WORD originalIP = getIP();
    WORD flags = getFlags();
    WORD selector = readPhysicalMemory<WORD>(PhysicalAddress(isr * 4 + 2));
    WORD offset = readPhysicalMemory<WORD>(PhysicalAddress(isr * 4));

    if (options.trapint)
        vlog(LogCPU, "PE=0 interrupt %02x,%04x%s -> %04x:%04x", isr, getAX(), source == InterruptSource::External ? " (external)" : "", selector, offset);

#ifdef LOG_FAR_JUMPS
    vlog(LogCPU, "[PE=0] Interrupt from %04x:%08x to %04x:%08x", getBaseCS(), currentBaseInstructionPointer(), selector, offset);
#endif

    setCS(selector);
    setEIP(offset);

    push16(flags);
    push16(originalCS);
    push16(originalIP);

    setIF(0);
    setTF(0);
}

void CPU::protectedModeInterrupt(BYTE isr, InterruptSource source, QVariant errorCode)
{
    ASSERT(getPE());

    if (source == InterruptSource::Internal && getVM() && getIOPL() != 3) {
        throw GeneralProtectionFault(0, "Software INT in VM86 mode with IOPL != 3");
    }

    auto idtEntry = getInterruptDescriptor(isr);
    if (!idtEntry.isTaskGate() && !idtEntry.isTrapGate() && !idtEntry.isInterruptGate()) {
        throw GeneralProtectionFault(makeErrorCode(isr, 1, source), "Interrupt to invalid gate type");
    }
    auto& gate = idtEntry.asGate();

    if (source == InterruptSource::Internal) {
        if (gate.DPL() < getCPL()) {
            throw GeneralProtectionFault(makeErrorCode(isr, 1, source), QString("Software interrupt trying to escalate privilege (CPL=%1, DPL=%2, VM=%3)").arg(getCPL()).arg(gate.DPL()).arg(getVM()));
        }
    }

    if (!gate.present()) {
        throw NotPresent(makeErrorCode(isr, 1, source), "Interrupt gate not present");
    }

    if (gate.isNull()) {
        throw GeneralProtectionFault(makeErrorCode(isr, 1, source), "Interrupt gate is null");
    }

    auto entry = gate.entry();

    if (options.trapint) {
        vlog(LogCPU, "PE=1 interrupt %02x,%04x%s, type: %s (%1x), %04x:%08x", isr, getAX(), source == InterruptSource::External ? " (external)" : "", gate.typeName(), gate.type(), entry.selector(), entry.offset());
        dumpDescriptor(gate);
    }

    if (gate.isTaskGate()) {
        interruptToTaskGate(isr, source, errorCode, gate);
        return;
    }

    auto descriptor = getDescriptor(gate.selector(), SegmentRegisterIndex::CS);

    if (options.trapint) {
        dumpDescriptor(descriptor);
    }

    if (descriptor.isNull()) {
        throw GeneralProtectionFault(source == InterruptSource::External, "Interrupt gate to null descriptor");
    }

    if (descriptor.isOutsideTableLimits()) {
        throw GeneralProtectionFault(makeErrorCode(gate.selector(), 0, source), "Interrupt gate to descriptor outside table limit");
    }

    if (!descriptor.isCode()) {
        dumpDescriptor(descriptor);
        throw GeneralProtectionFault(makeErrorCode(gate.selector(), 0, source), "Interrupt gate to non-code segment");
    }

    auto& codeDescriptor = descriptor.asCodeSegmentDescriptor();
    if (codeDescriptor.DPL() > getCPL()) {
        throw GeneralProtectionFault(makeErrorCode(gate.selector(), 0, source), QString("Interrupt gate to segment with DPL(%1)>CPL(%2)").arg(codeDescriptor.DPL()).arg(getCPL()));
    }

    if (!codeDescriptor.present()) {
        throw NotPresent(makeErrorCode(gate.selector(), 0, source), "Interrupt to non-present segment");
    }

    DWORD offset = gate.offset();
    DWORD flags = getEFlags();

    WORD originalSS = getSS();
    DWORD originalESP = getESP();
    WORD originalCPL = getCPL();
    WORD originalCS = getCS();
    DWORD originalEIP = getEIP();

    if (!gate.is32Bit() || !codeDescriptor.is32Bit()) {
        if (offset & 0xffff0000) {
            vlog(LogCPU, "Truncating interrupt entry offset from %04x:%08x to %04x:%08x", gate.selector(), offset, gate.selector(), offset & 0xffff);
        }
        offset &= 0xffff;
    }

    // FIXME: Stack-related exceptions should come before this.
    if (offset > codeDescriptor.effectiveLimit()) {
        throw GeneralProtectionFault(0, "Offset outside segment limit");
    }

    if (getVM()) {
        interruptFromVM86Mode(gate, offset, codeDescriptor, source, errorCode);
        return;
    }

    if (!codeDescriptor.conforming() && descriptor.DPL() < originalCPL) {
#ifdef DEBUG_JUMPS
        vlog(LogCPU, "Interrupt escalating privilege from ring%u to ring%u", originalCPL, descriptor.DPL(), descriptor);
#endif
        auto tss = currentTSS();

        WORD newSS = tss.getRingSS(descriptor.DPL());
        DWORD newESP = tss.getRingESP(descriptor.DPL());
        auto newSSDescriptor = getDescriptor(newSS, SegmentRegisterIndex::SS);

        if (newSSDescriptor.isNull()) {
            throw InvalidTSS(source == InterruptSource::External, "New ss is null");
        }

        if (newSSDescriptor.isOutsideTableLimits()) {
            throw InvalidTSS(makeErrorCode(newSS, 0, source), "New ss outside table limits");
        }

        if (newSSDescriptor.DPL() != descriptor.DPL()) {
            throw InvalidTSS(makeErrorCode(newSS, 0, source), QString("New ss DPL(%1) != code segment DPL(%2)").arg(newSSDescriptor.DPL()).arg(descriptor.DPL()));
        }

        if (!newSSDescriptor.isData() || !newSSDescriptor.asDataSegmentDescriptor().writable()) {
            throw InvalidTSS(makeErrorCode(newSS, 0, source), "New ss not a writable data segment");
        }

        if (!newSSDescriptor.present()) {
            throw StackFault(makeErrorCode(newSS, 0, source), "New ss not present");
        }

        BEGIN_ASSERT_NO_EXCEPTIONS
        setCPL(descriptor.DPL());
        setSS(newSS);
        setESP(newESP);

#ifdef DEBUG_JUMPS
        vlog(LogCPU, "Interrupt to inner ring, ss:esp %04x:%08x -> %04x:%08x", originalSS, originalESP, getSS(), getESP());
        vlog(LogCPU, "Push %u-bit ss:esp %04x:%08x @stack{%04x:%08x}", gate.size(), originalSS, originalESP, getSS(), getESP());
#endif
        pushValueWithSize(originalSS, gate.size());
        pushValueWithSize(originalESP, gate.size());
        END_ASSERT_NO_EXCEPTIONS
    } else if (codeDescriptor.conforming() || codeDescriptor.DPL() == originalCPL) {
#ifdef DEBUG_JUMPS
        vlog(LogCPU, "Interrupt same privilege from ring%u to ring%u", originalCPL, descriptor.DPL());
#endif
        if (getVM() && (codeDescriptor.conforming() || codeDescriptor.DPL() != 0)) {
            ASSERT_NOT_REACHED();
            throw GeneralProtectionFault(gate.selector() & ~3, "Interrupt in VM86 mode to code segment with DPL != 0");
        }


        setCPL(originalCPL);
    } else {
        ASSERT_NOT_REACHED();
        throw GeneralProtectionFault(makeErrorCode(gate.selector(), 0, source), "Interrupt to non-conforming code segment with DPL > CPL");
    }

#ifdef DEBUG_JUMPS
    vlog(LogCPU, "Push %u-bit flags %08x @stack{%04x:%08x}", gate.size(), flags, getSS(), getESP());
    vlog(LogCPU, "Push %u-bit cs:eip %04x:%08x @stack{%04x:%08x}", gate.size(), originalCS, originalEIP, getSS(), getESP());
#endif
    BEGIN_ASSERT_NO_EXCEPTIONS
    pushValueWithSize(flags, gate.size());
    pushValueWithSize(originalCS, gate.size());
    pushValueWithSize(originalEIP, gate.size());
    if (errorCode.isValid()) {
        pushValueWithSize(errorCode.value<WORD>(), gate.size());
    }

    if (gate.isInterruptGate())
        setIF(0);
    setTF(0);
    setRF(0);
    setNT(0);
    setVM(0);
    setCS(gate.selector());
    setEIP(offset);
    END_ASSERT_NO_EXCEPTIONS
}

void CPU::interruptFromVM86Mode(Gate& gate, DWORD offset, CodeSegmentDescriptor& codeDescriptor, InterruptSource source, QVariant errorCode)
{
    vlog(LogCPU, "Interrupt from VM86 mode -> %04x:%08x", gate.selector(), offset);

    DWORD originalFlags = getEFlags();
    WORD originalSS = getSS();
    DWORD originalESP = getESP();

    if (codeDescriptor.DPL() != 0) {
        throw GeneralProtectionFault(makeErrorCode(gate.selector(), 0, source), "Interrupt from VM86 mode to descriptor with CPL != 0");
    }

    auto tss = currentTSS();

    WORD newSS = tss.getSS0();
    DWORD newESP = tss.getESP0();
    auto newSSDescriptor = getDescriptor(newSS, SegmentRegisterIndex::SS);

    if (newSSDescriptor.isNull()) {
        throw InvalidTSS(source == InterruptSource::External, "New ss is null");
    }

    if (newSSDescriptor.isOutsideTableLimits()) {
        throw InvalidTSS(makeErrorCode(newSS, 0, source), "New ss outside table limits");
    }

    if ((newSS & 3) != 0) {
        throw InvalidTSS(makeErrorCode(newSS, 0, source), QString("New ss RPL(%1) != 0").arg(newSS & 3));
    }

    if (newSSDescriptor.DPL() != 0) {
        throw InvalidTSS(makeErrorCode(newSS, 0, source), QString("New ss DPL(%1) != 0").arg(newSSDescriptor.DPL()));
    }

    if (!newSSDescriptor.isData() || !newSSDescriptor.asDataSegmentDescriptor().writable()) {
        throw InvalidTSS(makeErrorCode(newSS, 0, source), "New ss not a writable data segment");
    }

    if (!newSSDescriptor.present()) {
        throw StackFault(makeErrorCode(newSS, 0, source), "New ss not present");
    }

    BEGIN_ASSERT_NO_EXCEPTIONS
    setCPL(0);
    setVM(0);
    setTF(0);
    setRF(0);
    setNT(0);
    if (gate.isInterruptGate())
        setIF(0);
    setSS(newSS);
    setESP(newESP);
    pushValueWithSize(getGS(), gate.size());
    pushValueWithSize(getFS(), gate.size());
    pushValueWithSize(getDS(), gate.size());
    pushValueWithSize(getES(), gate.size());
    pushValueWithSize(originalSS, gate.size());
    pushValueWithSize(originalESP, gate.size());
    pushValueWithSize(originalFlags, gate.size());
    pushValueWithSize(getCS(), gate.size());
    pushValueWithSize(getEIP(), gate.size());
    if (errorCode.isValid()) {
        pushValueWithSize(errorCode.value<WORD>(), gate.size());
    }
    setGS(0);
    setFS(0);
    setDS(0);
    setES(0);
    setCS(gate.selector());
    setCPL(0);
    setEIP(offset);
    END_ASSERT_NO_EXCEPTIONS
}

void CPU::interrupt(BYTE isr, InterruptSource source, QVariant errorCode)
{
    if (getPE())
        protectedModeInterrupt(isr, source, errorCode);
    else
        realModeInterrupt(isr, source);
}

void CPU::protectedIRET(TransactionalPopper& popper, LogicalAddress address)
{
    ASSERT(getPE());
#ifdef DEBUG_JUMPS
    WORD originalSS = getSS();
    DWORD originalESP = getESP();
    WORD originalCS = getCS();
    DWORD originalEIP = getEIP();
#endif

    WORD selector = address.selector();
    DWORD offset = address.offset();
    WORD originalCPL = getCPL();
    BYTE selectorRPL = selector & 3;

#ifdef LOG_FAR_JUMPS
    vlog(LogCPU, "[PE=%u, PG=%u] IRET from %04x:%08x to %04x:%08x", getPE(), getPG(), getBaseCS(), currentBaseInstructionPointer(), selector, offset);
#endif

    auto descriptor = getDescriptor(selector, SegmentRegisterIndex::CS);

    if (descriptor.isNull())
        throw GeneralProtectionFault(0, "IRET to null selector");

    if (descriptor.isOutsideTableLimits())
        throw GeneralProtectionFault(selector, "IRET to selector outside table limit");

    if (!descriptor.isCode()) {
        dumpDescriptor(descriptor);
        throw GeneralProtectionFault(selector, "Not a code segment");
    }

    if (selectorRPL < getCPL())
        throw GeneralProtectionFault(selector, QString("IRET with RPL(%1) < CPL(%2)").arg(selectorRPL).arg(getCPL()));

    auto& codeSegment = descriptor.asCodeSegmentDescriptor();

    if (codeSegment.conforming() && codeSegment.DPL() > selectorRPL)
        throw GeneralProtectionFault(selector, "IRET to conforming code segment with DPL > RPL");

    if (!codeSegment.conforming() && codeSegment.DPL() != selectorRPL)
        throw GeneralProtectionFault(selector, "IRET to non-conforming code segment with DPL != RPL");

    if (!codeSegment.present())
        throw NotPresent(selector, "Code segment not present");

    // NOTE: A 32-bit jump into a 16-bit segment might have irrelevant higher bits set.
    // Mask them off to make sure we don't incorrectly fail limit checks.
    if (!codeSegment.is32Bit())
        offset &= 0xffff;

    if (offset > codeSegment.effectiveLimit()) {
        vlog(LogCPU, "IRET to eip(%08x) outside limit(%08x)", offset, codeSegment.effectiveLimit());
        dumpDescriptor(codeSegment);
        throw GeneralProtectionFault(0, "Offset outside segment limit");
    }

    // FIXME: Validate SS before clobbering CS:EIP.
    setCS(selector);
    setEIP(offset);

    if (selectorRPL > originalCPL) {
        BEGIN_ASSERT_NO_EXCEPTIONS
        DWORD newESP = popper.popOperandSizedValue();
        WORD newSS = popper.popOperandSizedValue();
#ifdef DEBUG_JUMPS
        vlog(LogCPU, "Popped %u-bit ss:esp %04x:%08x @stack{%04x:%08x}", o16() ? 16 : 32, newSS, newESP, getSS(), popper.adjustedStackPointer());
        vlog(LogCPU, "IRET from ring%u to ring%u, ss:esp %04x:%08x -> %04x:%08x", originalCPL, getCPL(), originalSS, originalESP, newSS, newESP);
#endif

        setSS(newSS);
        setESP(newESP);

        clearSegmentRegisterAfterReturnIfNeeded(SegmentRegisterIndex::ES, JumpType::IRET);
        clearSegmentRegisterAfterReturnIfNeeded(SegmentRegisterIndex::FS, JumpType::IRET);
        clearSegmentRegisterAfterReturnIfNeeded(SegmentRegisterIndex::GS, JumpType::IRET);
        clearSegmentRegisterAfterReturnIfNeeded(SegmentRegisterIndex::DS, JumpType::IRET);
        END_ASSERT_NO_EXCEPTIONS
    } else {
        popper.commit();
    }
}

void CPU::iretToVM86Mode(TransactionalPopper& popper, LogicalAddress entry, DWORD flags)
{
    vlog(LogCPU, "IRET (o%u) to VM86 mode -> %04x:%04x", o16() ? 16 : 32, entry.selector(), entry.offset());
    if (!o32()) {
        vlog(LogCPU, "Hmm, o16 IRET to VM86!?");
        ASSERT_NOT_REACHED();
    }

    if (entry.offset() & 0xffff0000)
        throw GeneralProtectionFault(0, "IRET to VM86 with offset > 0xffff");

    setEFlags(flags);
    setCS(entry.selector());
    setEIP(entry.offset());

    DWORD newESP = popper.pop32();
    WORD newSS = popper.pop32();
    setES(popper.pop32());
    setDS(popper.pop32());
    setFS(popper.pop32());
    setGS(popper.pop32());
    setCPL(3);
    setESP(newESP);
    setSS(newSS);
}
