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
#include "debugger.h"
#include "Tasking.h"

void CPU::_STR_RM16(Instruction& insn)
{
    if (!getPE() || getVM()) {
        throw InvalidOpcode("STR not recognized in real/VM86 mode");
    }
    insn.modrm().writeClearing16(TR.selector, o32());
}

void CPU::_LTR_RM16(Instruction& insn)
{
    if (!getPE() || getVM()) {
        throw InvalidOpcode("LTR not recognized in real/VM86 mode");
    }

    if (getCPL() != 0)
        throw GeneralProtectionFault(0, "LTR with CPL != 0");

    WORD selector = insn.modrm().read16();
    auto descriptor = getDescriptor(selector);

    if (getCPL() != 0) {
        throw GeneralProtectionFault(0, QString("LTR with CPL(%u)!=0").arg(getCPL()));
    }
    if (!descriptor.isGlobal()) {
        throw GeneralProtectionFault(selector, "LTR selector must reference GDT");
    }
    if (!descriptor.isTSS()) {
        throw GeneralProtectionFault(selector, "LTR with non-TSS descriptor");
    }
    auto& tssDescriptor = descriptor.asTSSDescriptor();
    if (tssDescriptor.isBusy()) {
        throw GeneralProtectionFault(selector, "LTR with busy TSS");
    }
    if (!tssDescriptor.present()) {
        throw NotPresent(selector, "LTR with non-present TSS");
    }

    tssDescriptor.setBusy();
    writeToGDT(tssDescriptor);

    TR.selector = selector;
    TR.base = tssDescriptor.base();
    TR.limit = tssDescriptor.limit();
    TR.is32Bit = tssDescriptor.is32Bit();
#ifdef DEBUG_TASK_SWITCH
    vlog(LogAlert, "LTR { segment: %04x => base:%08x, limit:%08x }", TR.selector, TR.base.get(), TR.limit);
#endif
}

#define EXCEPTION_ON(type, code, condition, reason) \
    do { \
        if ((condition)) { \
            throw type(code, reason); \
        } \
    } while(0)

void CPU::taskSwitch(TSSDescriptor& incomingTSSDescriptor, JumpType source)
{
    ASSERT(incomingTSSDescriptor.is32Bit());

    EXCEPTION_ON(GeneralProtectionFault, 0, incomingTSSDescriptor.isNull(), "Incoming TSS descriptor is null");
    EXCEPTION_ON(GeneralProtectionFault, 0, !incomingTSSDescriptor.isGlobal(), "Incoming TSS descriptor is not from GDT");
    EXCEPTION_ON(NotPresent, 0, !incomingTSSDescriptor.present(), "Incoming TSS descriptor is not present");
    EXCEPTION_ON(GeneralProtectionFault, 0, incomingTSSDescriptor.limit() < 103, "Incoming TSS descriptor limit too small");

    if (source == JumpType::IRET) {
        EXCEPTION_ON(GeneralProtectionFault, 0, incomingTSSDescriptor.isAvailable(), "Incoming TSS descriptor is available");
    } else {
        EXCEPTION_ON(GeneralProtectionFault, 0, incomingTSSDescriptor.isBusy(), "Incoming TSS descriptor is busy");
    }

    auto outgoingDescriptor = getDescriptor(TR.selector);
    if (!outgoingDescriptor.isTSS()) {
        // Hmm, what have we got ourselves into now?
        vlog(LogCPU, "Switching tasks and outgoing TSS is not a TSS:");
        dumpDescriptor(outgoingDescriptor);
    }

    TSSDescriptor outgoingTSSDescriptor = outgoingDescriptor.asTSSDescriptor();
    ASSERT(outgoingTSSDescriptor.isTSS());

    if (outgoingTSSDescriptor.base() == incomingTSSDescriptor.base())
        vlog(LogCPU, "Switching to same TSS (%08x)", incomingTSSDescriptor.base().get());

    TSS outgoingTSS(*this, TR.base, outgoingTSSDescriptor.is32Bit());

    outgoingTSS.setEAX(getEAX());
    outgoingTSS.setEBX(getEBX());
    outgoingTSS.setECX(getECX());
    outgoingTSS.setEDX(getEDX());
    outgoingTSS.setEBP(getEBP());
    outgoingTSS.setESP(getESP());
    outgoingTSS.setESI(getESI());
    outgoingTSS.setEDI(getEDI());

    if (source == JumpType::JMP || source == JumpType::IRET) {
        outgoingTSSDescriptor.setAvailable();
        writeToGDT(outgoingTSSDescriptor);
    }

    DWORD outgoingEFlags = getEFlags();

    if (source == JumpType::IRET) {
        outgoingEFlags &= ~Flag::NT;
    }

    outgoingTSS.setEFlags(outgoingEFlags);

    outgoingTSS.setCS(getCS());
    outgoingTSS.setDS(getDS());
    outgoingTSS.setES(getES());
    outgoingTSS.setFS(getFS());
    outgoingTSS.setGS(getGS());
    outgoingTSS.setSS(getSS());
    outgoingTSS.setLDT(LDTR.selector);
    outgoingTSS.setEIP(getEIP());

    if (getPG())
        outgoingTSS.setCR3(getCR3());

    TSS incomingTSS(*this, incomingTSSDescriptor.base(), incomingTSSDescriptor.is32Bit());

#ifdef DEBUG_TASK_SWITCH
    vlog(LogCPU, "Outgoing TSS @ %08x:", outgoingTSSDescriptor.base());
    dumpTSS(outgoingTSS);
    vlog(LogCPU, "Incoming TSS @ %08x:", incomingTSSDescriptor.base());
    dumpTSS(incomingTSS);
#endif

    // First, load all registers from TSS without validating contents.
    if (getPG()) {
        m_CR3 = incomingTSS.getCR3();
    }

    LDTR.selector = incomingTSS.getLDT();
    LDTR.base = LinearAddress();
    LDTR.limit = 0;

    CS = incomingTSS.getCS();
    DS = incomingTSS.getDS();
    ES = incomingTSS.getES();
    FS = incomingTSS.getFS();
    GS = incomingTSS.getGS();
    SS = incomingTSS.getSS();

    DWORD incomingEFlags = incomingTSS.getEFlags();

    if (incomingEFlags & Flag::VM) {
        vlog(LogCPU, "Incoming task is in VM86 mode, this needs work!");
        ASSERT_NOT_REACHED();
    }

    if (source == JumpType::CALL || source == JumpType::INT) {
        incomingEFlags |= Flag::NT;
    }

    if (incomingTSS.is32Bit())
        setEFlags(incomingEFlags);
    else
        setFlags(incomingEFlags);

    setEAX(incomingTSS.getEAX());
    setEBX(incomingTSS.getEBX());
    setECX(incomingTSS.getECX());
    setEDX(incomingTSS.getEDX());
    setEBP(incomingTSS.getEBP());
    setESP(incomingTSS.getESP());
    setESI(incomingTSS.getESI());
    setEDI(incomingTSS.getEDI());

    if (source == JumpType::CALL || source == JumpType::INT) {
        incomingTSS.setBacklink(TR.selector);
    }

    TR.selector = incomingTSSDescriptor.index();
    TR.base = incomingTSSDescriptor.base();
    TR.limit = incomingTSSDescriptor.limit();
    TR.is32Bit = incomingTSSDescriptor.is32Bit();

    if (source != JumpType::IRET) {
        incomingTSSDescriptor.setBusy();
        writeToGDT(incomingTSSDescriptor);
    }

    m_CR0 |= CR0::TS; // Task Switched

    // Now, let's validate!
    auto ldtDescriptor = getDescriptor(LDTR.selector);
    if (!ldtDescriptor.isNull()) {
        if (!ldtDescriptor.isGlobal())
            throw InvalidTSS(LDTR.selector & 0xfffc, "Incoming LDT is not in GDT");
        if (!ldtDescriptor.isLDT())
            throw InvalidTSS(LDTR.selector & 0xfffc, "Incoming LDT is not an LDT");
    }

    unsigned incomingCPL = getCS() & 3;

    auto csDescriptor = getDescriptor(CS);
    if (csDescriptor.isCode()) {
        if (csDescriptor.isNonconformingCode()) {
            if (csDescriptor.DPL() != (getCS() & 3))
                throw InvalidTSS(getCS(), QString("CS is non-conforming with DPL(%1) != RPL(%2)").arg(csDescriptor.DPL()).arg(getCS() & 3));
        } else if (csDescriptor.isConformingCode()) {
            if (csDescriptor.DPL() > (getCS() & 3))
                throw InvalidTSS(getCS(), "CS is conforming with DPL > RPL");
        }
    }
    auto ssDescriptor = getDescriptor(getSS());
    if (!ssDescriptor.isNull()) {
        if (ssDescriptor.isOutsideTableLimits())
            throw InvalidTSS(getSS(), "SS outside table limits");
        if (!ssDescriptor.isData())
            throw InvalidTSS(getSS(), "SS is not a data segment");
        if (!ssDescriptor.asDataSegmentDescriptor().writable())
            throw InvalidTSS(getSS(), "SS is not writable");
        if (!ssDescriptor.present())
            throw StackFault(getSS(), "SS is not present");
        if (ssDescriptor.DPL() != incomingCPL)
            throw InvalidTSS(getSS(), QString("SS DPL(%1) != CPL(%2)").arg(ssDescriptor.DPL()).arg(incomingCPL));
    }

    if (!ldtDescriptor.isNull()) {
        if (!ldtDescriptor.present())
            throw InvalidTSS(LDTR.selector & 0xfffc, "Incoming LDT is not present");
    }

    if (!csDescriptor.isCode())
        throw InvalidTSS(getCS(), "CS is not a code segment");
    if (!csDescriptor.present())
        throw InvalidTSS(getCS(), "CS is not present");

    if (ssDescriptor.DPL() != (getSS() & 3))
        throw InvalidTSS(getSS(), "SS DPL != RPL");

    auto validateDataSegment = [&] (SegmentRegisterIndex segreg) {
        WORD selector = readSegmentRegister(segreg);
        auto descriptor = getDescriptor(selector);
        if (descriptor.isNull())
            return;
        if (descriptor.isOutsideTableLimits())
            throw InvalidTSS(selector, "DS/ES/FS/GS outside table limits");
        if (!descriptor.isSegmentDescriptor())
            throw InvalidTSS(selector, "DS/ES/FS/GS is a system segment");
        if (!descriptor.present())
            throw NotPresent(selector, "DS/ES/FS/GS is not present");
        if (!descriptor.isConformingCode() && descriptor.DPL() < incomingCPL)
            throw InvalidTSS(selector, "DS/ES/FS/GS has DPL < CPL and is not a conforming code segment");
    };

    validateDataSegment(SegmentRegisterIndex::DS);
    validateDataSegment(SegmentRegisterIndex::ES);
    validateDataSegment(SegmentRegisterIndex::FS);
    validateDataSegment(SegmentRegisterIndex::GS);

    EXCEPTION_ON(GeneralProtectionFault, 0, getEIP() > cachedDescriptor(SegmentRegisterIndex::CS).effectiveLimit(), "Task switch to EIP outside CS limit");

    setLDT(incomingTSS.getLDT());
    setCS(incomingTSS.getCS());
    setES(incomingTSS.getES());
    setDS(incomingTSS.getDS());
    setFS(incomingTSS.getFS());
    setGS(incomingTSS.getGS());
    setSS(incomingTSS.getSS());
    setEIP(incomingTSS.getEIP());

    if (getTF()) {
        vlog(LogCPU, "Leaving task switch with TF=1");
    }

    if (getVM()) {
        vlog(LogCPU, "Leaving task switch with VM=1");
    }

#ifdef DEBUG_TASK_SWITCH
    vlog(LogCPU, "Task switched to %08x, cpl=%u, iopl=%u", incomingTSSDescriptor.base(), getCPL(), getIOPL());
#endif
}

void CPU::dumpTSS(const TSS &tss)
{
    vlog(LogCPU, "TSS bits=%u", tss.is32Bit() ? 32 : 16);
    vlog(LogCPU, "eax=%08x ebx=%08x ecx=%08x edx=%08x", tss.getEAX(), tss.getEBX(), tss.getECX(), tss.getEDX());
    vlog(LogCPU, "esi=%08x edi=%08x ebp=%08x esp=%08x", tss.getESI(), tss.getEDI(), tss.getEBP(), tss.getESP());
    vlog(LogCPU, "ldt=%04x backlink=%04x cr3=%08x", tss.getLDT(), tss.getBacklink(), getPG() ? tss.getCR3() : 0);
    vlog(LogCPU, "ds=%04x ss=%04x es=%04x fs=%04x gs=%04x", tss.getDS(), tss.getSS(), tss.getES(), tss.getFS(), tss.getGS());
    vlog(LogCPU, "cs=%04x eip=%08x eflags=%08x", tss.getCS(), tss.getEIP(), tss.getEFlags());
    vlog(LogCPU, "stack0 { %04x:%08x }", tss.getSS0(), tss.getESP0());
    vlog(LogCPU, "stack1 { %04x:%08x }", tss.getSS1(), tss.getESP1());
    vlog(LogCPU, "stack2 { %04x:%08x }", tss.getSS2(), tss.getESP2());

#ifdef DEBUG_TASK_SWITCH
    bool isGlobal = (tss.getCS() & 0x04) == 0;
    auto newCSDesc = isGlobal
            ? getDescriptor(tss.getCS(), SegmentRegisterIndex::CS)
            : getDescriptor("LDT", getDescriptor(tss.getLDT()).asLDTDescriptor().base(), getDescriptor(tss.getLDT()).asLDTDescriptor().limit(), tss.getCS(), true);

    vlog(LogCPU, "cpl=%u {%u} iopl=%u", tss.getCS() & 3, newCSDesc.DPL(), ((tss.getEFlags() >> 12) & 3));
#endif
}

void CPU::taskSwitch(WORD task, JumpType source)
{
    auto descriptor = getDescriptor(task);
    auto& tssDescriptor = descriptor.asTSSDescriptor();
    taskSwitch(tssDescriptor, source);
}

TSS CPU::currentTSS()
{
    return TSS(*this, TR.base, TR.is32Bit);
}

struct TSS32 {
    WORD Backlink, __blh;
    DWORD ESP0;
    WORD SS0, __ss0h;
    DWORD ESP1;
    WORD SS1, __ss1h;
    DWORD ESP2;
    WORD SS2, __ss2h;
    DWORD CR3, EIP, EFlags;
    DWORD EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI;
    WORD ES, __esh;
    WORD CS, __csh;
    WORD SS, __ssh;
    WORD DS, __dsh;
    WORD FS, __fsh;
    WORD GS, __gsh;
    WORD LDT, __ldth;
    WORD trace, iomapbase;
} __attribute__ ((packed));

struct TSS16 {
    WORD Backlink;
    WORD SP0;
    WORD SS0;
    WORD SP1;
    WORD SS1;
    WORD SP2;
    WORD SS2;
    WORD IP;
    WORD Flags;
    WORD AX, CX, DX, BX, SP, BP, SI, DI;
    WORD ES;
    WORD CS;
    WORD SS;
    WORD DS;
    WORD FS;
    WORD GS;
    WORD LDT;
} __attribute__ ((packed));

TSS::TSS(CPU& cpu, LinearAddress base, bool is32Bit)
    : m_cpu(cpu)
    , m_base(base)
    , m_is32Bit(is32Bit)
{
}

#define TSS_FIELD_16(name) \
    void TSS::set ## name(WORD value) { \
        if (m_is32Bit) \
            m_cpu.writeMemory16(m_base.offset(offsetof(TSS32, name)), value); \
        else \
            m_cpu.writeMemory16(m_base.offset(offsetof(TSS16, name)), value); \
    } \
    WORD TSS::get ## name() const { \
        if (m_is32Bit) \
            return m_cpu.readMemory16(m_base.offset(offsetof(TSS32, name))); \
        return m_cpu.readMemory16(m_base.offset(offsetof(TSS16, name))); \
    }

#define TSS_FIELD_16OR32(name) \
    DWORD TSS::getE ## name() const { \
        if (m_is32Bit) \
            return m_cpu.readMemory32(m_base.offset(offsetof(TSS32, E ## name))); \
        return m_cpu.readMemory16(m_base.offset(offsetof(TSS16, name))); \
    } \
    void TSS::setE ## name(DWORD value) { \
        if (m_is32Bit) \
            m_cpu.writeMemory32(m_base.offset(offsetof(TSS32, E ## name)), value); \
        else \
            m_cpu.writeMemory16(m_base.offset(offsetof(TSS16, name)), value); \
    }

DWORD TSS::getCR3() const
{
    ASSERT(m_is32Bit);
    return m_cpu.readMemory32(m_base.offset(offsetof(TSS32, CR3)));
}

void TSS::setCR3(DWORD value)
{
    ASSERT(m_is32Bit);
    m_cpu.writeMemory32(m_base.offset(offsetof(TSS32, CR3)), value);
}

TSS_FIELD_16OR32(AX)
TSS_FIELD_16OR32(BX)
TSS_FIELD_16OR32(CX)
TSS_FIELD_16OR32(DX)
TSS_FIELD_16OR32(SI)
TSS_FIELD_16OR32(DI)
TSS_FIELD_16OR32(BP)
TSS_FIELD_16OR32(SP)
TSS_FIELD_16OR32(IP)
TSS_FIELD_16OR32(SP0)
TSS_FIELD_16OR32(SP1)
TSS_FIELD_16OR32(SP2)
TSS_FIELD_16OR32(Flags)

TSS_FIELD_16(Backlink)
TSS_FIELD_16(LDT)
TSS_FIELD_16(CS)
TSS_FIELD_16(DS)
TSS_FIELD_16(ES)
TSS_FIELD_16(SS)
TSS_FIELD_16(FS)
TSS_FIELD_16(GS)
TSS_FIELD_16(SS0)
TSS_FIELD_16(SS1)
TSS_FIELD_16(SS2)

DWORD TSS::getRingESP(BYTE ring) const
{
    if (ring == 0)
        return getESP0();
    if (ring == 1)
        return getESP1();
    if (ring == 2)
        return getESP2();
    ASSERT_NOT_REACHED();
    return 0;
}

WORD TSS::getRingSS(BYTE ring) const
{
    if (ring == 0)
        return getSS0();
    if (ring == 1)
        return getSS1();
    if (ring == 2)
        return getSS2();
    ASSERT_NOT_REACHED();
    return 0;
}

WORD TSS::getIOMapBase() const
{
    ASSERT(m_is32Bit);
    return m_cpu.readMemory16(m_base.offset(offsetof(TSS32, iomapbase)));
}
