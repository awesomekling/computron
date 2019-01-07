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

//#define DEBUG_DESCRIPTOR_TABLES

void CPU::doSGDTorSIDT(Instruction& insn, DescriptorTableRegister& table)
{
    if (insn.modrm().isRegister())
        throw InvalidOpcode(QString("%1 with register destination").arg(insn.mnemonic()));

    snoop(insn.modrm().segment(), insn.modrm().offset(), MemoryAccessType::Write);
    snoop(insn.modrm().segment(), insn.modrm().offset() + 6, MemoryAccessType::Write);
    DWORD maskedBase = o16() ? (table.base().get() & 0x00ffffff) : table.base().get();
    writeMemory16(insn.modrm().segment(), insn.modrm().offset(), table.limit());
    writeMemory32(insn.modrm().segment(), insn.modrm().offset() + 2, maskedBase);
}


void CPU::_SGDT(Instruction& insn)
{
    doSGDTorSIDT(insn, m_GDTR);
}

void CPU::_SIDT(Instruction& insn)
{
    doSGDTorSIDT(insn, m_IDTR);
}

void CPU::_SLDT_RM16(Instruction& insn)
{
    if (!getPE() || getVM()) {
        throw InvalidOpcode("SLDT not recognized in real/VM86 mode");
    }
    insn.modrm().writeSpecial(m_LDTR.selector(), o32());
}

void CPU::setLDT(WORD selector)
{
    auto descriptor = getDescriptor(selector);
    LinearAddress base;
    WORD limit = 0;
    if (!descriptor.isNull()) {
        if (descriptor.isLDT()) {
            auto& ldtDescriptor = descriptor.asLDTDescriptor();
            if (!descriptor.present()) {
                throw NotPresent(selector & 0xfffc, "LDT segment not present");
            }
            base = ldtDescriptor.base();
            limit = ldtDescriptor.limit();
        } else {
            throw GeneralProtectionFault(selector & 0xfffc, "Not an LDT descriptor");
        }
    }

    m_LDTR.setSelector(selector);
    m_LDTR.setBase(base);
    m_LDTR.setLimit(limit);

#ifdef DEBUG_DESCRIPTOR_TABLES
    vlog(LogAlert, "setLDT { segment: %04X => base:%08X, limit:%08X }", m_LDTR.selector(), m_LDTR.base(), m_LDTR.limit());
#endif
}

void CPU::_LLDT_RM16(Instruction& insn)
{
    if (!getPE() || getVM()) {
        throw InvalidOpcode("LLDT not recognized in real/VM86 mode");
    }

    if (getCPL() != 0)
        throw GeneralProtectionFault(0, "LLDT with CPL != 0");

    setLDT(insn.modrm().read16());
#ifdef DEBUG_DESCRIPTOR_TABLES
    dumpLDT();
#endif
}

void CPU::dumpLDT()
{
    for (unsigned i = 0; i < m_LDTR.limit(); i += 8) {
        dumpDescriptor(getDescriptor(i | 4));
    }
}

void CPU::doLGDTorLIDT(Instruction& insn, DescriptorTableRegister& table)
{
    if (insn.modrm().isRegister())
        throw InvalidOpcode(QString("%1 with register source").arg(insn.mnemonic()));

    if (getCPL() != 0)
        throw GeneralProtectionFault(0, QString("%1 with CPL != 0").arg(insn.mnemonic()));

    DWORD base = readMemory32(insn.modrm().segment(), insn.modrm().offset() + 2);
    WORD limit = readMemory16(insn.modrm().segment(), insn.modrm().offset());
    DWORD baseMask = o32() ? 0xffffffff : 0x00ffffff;
    table.setBase(LinearAddress(base & baseMask));
    table.setLimit(limit);
}

void CPU::_LGDT(Instruction& insn)
{
    doLGDTorLIDT(insn, m_GDTR);
#ifdef DEBUG_DESCRIPTOR_TABLES
    vlog(LogAlert, "LGDT { base:%08X, limit:%08X }", m_GDTR.base().get(), m_GDTR.limit());
    dumpGDT();
#endif
}

void CPU::_LIDT(Instruction& insn)
{
    doLGDTorLIDT(insn, m_IDTR);
#if DEBUG_DESCRIPTOR_TABLES
    dumpIDT();
#endif
}

void CPU::dumpGDT()
{
    vlog(LogDump, "GDT { base:%08x, limit:%08x }", m_GDTR.base().get(), m_GDTR.limit());
    for (unsigned i = 0; i < m_GDTR.limit(); i += 8) {
        dumpDescriptor(getDescriptor(i));
    }
}

void CPU::dumpIDT()
{
    vlog(LogDump, "IDT { base:%08X, limit:%08X }", m_IDTR.base().get(), m_IDTR.limit());
    if (getPE()) {
        for (DWORD isr = 0; isr < (m_IDTR.limit() / 16); ++isr) {
            dumpDescriptor(getInterruptDescriptor(isr));
        }
    }
}

void CPU::_CLTS(Instruction&)
{
    if (getPE()) {
        if (getCPL() != 0) {
            throw GeneralProtectionFault(0, QString("CLTS with CPL!=0(%1)").arg(getCPL()));
        }
    }
    m_CR0 &= ~(1 << 3);
}

void CPU::_LMSW_RM16(Instruction& insn)
{
    if (getPE()) {
        if (getCPL() != 0) {
            throw GeneralProtectionFault(0, QString("LMSW with CPL!=0(%1)").arg(getCPL()));
        }
    }

    WORD msw = insn.modrm().read16();

    if (getPE()) {
        // LMSW cannot exit protected mode.
        msw |= CR0::PE;
    }

    m_CR0 = (m_CR0 & 0xFFFFFFF0) | (msw & 0x0F);
#ifdef PMODE_DEBUG
    vlog(LogCPU, "LMSW set CR0=%08X, PE=%u", getCR0(), getPE());
#endif
}

void CPU::_SMSW_RM16(Instruction& insn)
{
#ifdef PMODE_DEBUG
    vlog(LogCPU, "SMSW get LSW(CR0)=%04X, PE=%u", getCR0() & 0xFFFF, getPE());
#endif
    insn.modrm().writeSpecial(getCR0(), o32());
}

void CPU::_LAR_reg16_RM16(Instruction& insn)
{
    if (!getPE() || getVM())
        throw InvalidOpcode("LAR not recognized in real/VM86 mode");

    // FIXME: This has various ways it can fail, implement them.
    WORD selector = insn.modrm().read16() & 0xffff;
    WORD selectorRPL = selector & 3;
    auto descriptor = getDescriptor(selector);
    if (descriptor.isNull() || descriptor.isOutsideTableLimits() || descriptor.DPL() < getCPL() || descriptor.DPL() < selectorRPL) {
        setZF(0);
        return;
    }
    insn.reg16() = descriptor.m_high & 0xff00;
    setZF(1);
}

void CPU::_LAR_reg32_RM32(Instruction& insn)
{
    if (!getPE() || getVM())
        throw InvalidOpcode("LAR not recognized in real/VM86 mode");

    // FIXME: This has various ways it can fail, implement them.
    WORD selector = insn.modrm().read32() & 0xffff;
    WORD selectorRPL = selector & 3;
    auto descriptor = getDescriptor(selector);
    if (descriptor.isNull() || descriptor.isOutsideTableLimits() || descriptor.DPL() < getCPL() || descriptor.DPL() < selectorRPL) {
        setZF(0);
        return;
    }
    insn.reg32() = descriptor.m_high & 0x00ffff00;
    setZF(1);
}

static bool isValidDescriptorForLSL(const Descriptor& descriptor)
{
    if (descriptor.isNull())
        return true;
    if (descriptor.isOutsideTableLimits())
        return true;
    if (descriptor.isSegmentDescriptor())
        return true;

    switch (descriptor.asSystemDescriptor().type()) {
    case SystemDescriptor::AvailableTSS_16bit:
    case SystemDescriptor::LDT:
    case SystemDescriptor::BusyTSS_16bit:
    case SystemDescriptor::AvailableTSS_32bit:
    case SystemDescriptor::BusyTSS_32bit:
        return true;
    default:
        return false;
    }
}

void CPU::_LSL_reg16_RM16(Instruction& insn)
{
    if (!getPE() || getVM())
        throw InvalidOpcode("LSL not recognized in real/VM86 mode");

    WORD selector = insn.modrm().read16() & 0xffff;
    auto descriptor = getDescriptor(selector);
    // FIXME: This should also fail for conforming code segments somehow.
    if (!isValidDescriptorForLSL(descriptor)) {
        setZF(0);
        return;
    }

    DWORD effectiveLimit;
    if (descriptor.isLDT())
        effectiveLimit = descriptor.asLDTDescriptor().effectiveLimit();
    else if (descriptor.isTSS())
        effectiveLimit = descriptor.asTSSDescriptor().effectiveLimit();
    else
        effectiveLimit = descriptor.asSegmentDescriptor().effectiveLimit();
    insn.reg16() = effectiveLimit;
    setZF(1);
}

void CPU::_LSL_reg32_RM32(Instruction& insn)
{
    if (!getPE() || getVM())
        throw InvalidOpcode("LSL not recognized in real/VM86 mode");

    WORD selector = insn.modrm().read16() & 0xffff;
    auto descriptor = getDescriptor(selector);
    // FIXME: This should also fail for conforming code segments somehow.
    if (descriptor.isOutsideTableLimits()) {
        setZF(0);
        return;
    }
    DWORD effectiveLimit;
    if (descriptor.isLDT())
        effectiveLimit = descriptor.asLDTDescriptor().effectiveLimit();
    else if (descriptor.isTSS())
        effectiveLimit = descriptor.asTSSDescriptor().effectiveLimit();
    else
        effectiveLimit = descriptor.asSegmentDescriptor().effectiveLimit();
    insn.reg32() = effectiveLimit;
    setZF(1);
}

const char* toString(SegmentRegisterIndex segment)
{
    switch (segment) {
    case SegmentRegisterIndex::CS: return "CS";
    case SegmentRegisterIndex::DS: return "DS";
    case SegmentRegisterIndex::ES: return "ES";
    case SegmentRegisterIndex::SS: return "SS";
    case SegmentRegisterIndex::FS: return "FS";
    case SegmentRegisterIndex::GS: return "GS";
    default: break;
    }
    return nullptr;
}

void CPU::raiseException(const Exception& e)
{
    if (options.crashOnException) {
        dumpAll();
        vlog(LogAlert, "CRASH ON EXCEPTION");
        ASSERT_NOT_REACHED();
    }

    try {
        setEIP(currentBaseInstructionPointer());
        if (e.hasCode()) {
            interrupt(e.num(), InterruptSource::External, e.code());
        } else {
            interrupt(e.num(), InterruptSource::External);
        }
    } catch (Exception e) {
        ASSERT_NOT_REACHED();
    }
}

Exception CPU::GeneralProtectionFault(WORD code, const QString& reason)
{
    WORD selector = code & 0xfff8;
    bool TI = code & 4;
    bool I = code & 2;
    bool EX = code & 1;

    if (options.log_exceptions)
        vlog(LogCPU, "Exception: #GP(%04x) selector=%04X, TI=%u, I=%u, EX=%u :: %s", code, selector, TI, I, EX, qPrintable(reason));
    if (options.crashOnGPF) {
        dumpAll();
        vlog(LogAlert, "CRASH ON GPF");
        ASSERT_NOT_REACHED();
    }
    return Exception(0xd, code, reason);
}

Exception CPU::StackFault(WORD selector, const QString& reason)
{
    if (options.log_exceptions)
        vlog(LogCPU, "Exception: #SS(%04x) :: %s", selector, qPrintable(reason));
    return Exception(0xc, selector, reason);
}

Exception CPU::NotPresent(WORD selector, const QString& reason)
{
    if (options.log_exceptions)
        vlog(LogCPU, "Exception: #NP(%04x) :: %s", selector, qPrintable(reason));
    return Exception(0xb, selector, reason);
}

Exception CPU::InvalidOpcode(const QString& reason)
{
    if (options.log_exceptions)
        vlog(LogCPU, "Exception: #UD :: %s", qPrintable(reason));
    return Exception(0x6, reason);
}

Exception CPU::BoundRangeExceeded(const QString& reason)
{
    if (options.log_exceptions)
        vlog(LogCPU, "Exception: #BR :: %s", qPrintable(reason));
    return Exception(0x5, reason);
}

Exception CPU::InvalidTSS(WORD selector, const QString& reason)
{
    if (options.log_exceptions)
        vlog(LogCPU, "Exception: #TS(%04x) :: %s", selector, qPrintable(reason));
    return Exception(0xa, selector, reason);
}

Exception CPU::DivideError(const QString& reason)
{
    if (options.log_exceptions)
        vlog(LogCPU, "Exception: #DE :: %s", qPrintable(reason));
    return Exception(0x0, reason);
}

void CPU::validateSegmentLoad(SegmentRegisterIndex reg, WORD selector, const Descriptor& descriptor)
{
    if (!getPE() || getVM())
        return;

    BYTE selectorRPL = selector & 3;

    if (descriptor.isOutsideTableLimits()) {
        throw GeneralProtectionFault(selector & 0xfffc, "Selector outside table limits");
    }

    if (reg == SegmentRegisterIndex::SS) {
        if (descriptor.isNull()) {
            throw GeneralProtectionFault(0, "ss loaded with null descriptor");
        }
        if (selectorRPL != getCPL()) {
            throw GeneralProtectionFault(selector & 0xfffc, QString("ss selector RPL(%1) != CPL(%2)").arg(selectorRPL).arg(getCPL()));
        }
        if (!descriptor.isData() || !descriptor.asDataSegmentDescriptor().writable()) {
            throw GeneralProtectionFault(selector & 0xfffc, "ss loaded with something other than a writable data segment");
        }
        if (descriptor.DPL() != getCPL()) {
            throw GeneralProtectionFault(selector & 0xfffc, QString("ss selector leads to descriptor with DPL(%1) != CPL(%2)").arg(descriptor.DPL()).arg(getCPL()));
        }
        if (!descriptor.present()) {
            throw StackFault(selector & 0xfffc, "ss loaded with non-present segment");
        }
        return;
    }

    if (descriptor.isNull())
        return;

    if (reg == SegmentRegisterIndex::DS
        || reg == SegmentRegisterIndex::ES
        || reg == SegmentRegisterIndex::FS
        || reg == SegmentRegisterIndex::GS) {
        if (!descriptor.isData() && (descriptor.isCode() && !descriptor.asCodeSegmentDescriptor().readable())) {
            throw GeneralProtectionFault(selector & 0xfffc, QString("%1 loaded with non-data or non-readable code segment").arg(registerName(reg)));
        }
        if (descriptor.isData() || descriptor.isNonconformingCode()) {
            if (selectorRPL > descriptor.DPL()) {
                throw GeneralProtectionFault(selector & 0xfffc, QString("%1 loaded with data or non-conforming code segment and RPL > DPL").arg(registerName(reg)));
            }
            if (getCPL() > descriptor.DPL()) {
                throw GeneralProtectionFault(selector & 0xfffc, QString("%1 loaded with data or non-conforming code segment and CPL > DPL").arg(registerName(reg)));
            }
        }
        if (!descriptor.present()) {
            throw NotPresent(selector & 0xfffc, QString("%1 loaded with non-present segment").arg(registerName(reg)));
        }
    }

    if (!descriptor.isNull() && !descriptor.isSegmentDescriptor()) {
        dumpDescriptor(descriptor);
        throw GeneralProtectionFault(selector & 0xfffc, QString("%1 loaded with system segment").arg(registerName(reg)));
    }
}

void CPU::writeSegmentRegister(SegmentRegisterIndex segreg, WORD selector)
{
    if ((int)segreg >= 6) {
        throw InvalidOpcode("Write to invalid segment register");
    }

    Descriptor descriptor;
    if (!getPE() || getVM())
        descriptor = getRealModeOrVM86Descriptor(selector, segreg);
    else
        descriptor = getDescriptor(selector);

    validateSegmentLoad(segreg, selector, descriptor);

    *m_segmentMap[(int)segreg] = selector;

    if (descriptor.isNull()) {
        cachedDescriptor(segreg) = descriptor.asSegmentDescriptor();
        return;
    }

    ASSERT(descriptor.isSegmentDescriptor());
    cachedDescriptor(segreg) = descriptor.asSegmentDescriptor();
    if (options.pedebug) {
        if (getPE()) {
            vlog(LogCPU, "%s loaded with %04x { type:%02X, base:%08X, limit:%08X }",
                toString(segreg),
                selector,
                descriptor.asSegmentDescriptor().type(),
                descriptor.asSegmentDescriptor().base(),
                descriptor.asSegmentDescriptor().limit()
            );
        }
    }

    switch (segreg) {
    case SegmentRegisterIndex::CS:
        if (getPE()) {
            if (getVM())
                setCPL(3);
            else
                setCPL(descriptor.DPL());
        }
        updateDefaultSizes();
        updateCodeSegmentCache();
        break;
    case SegmentRegisterIndex::SS:
        cachedDescriptor(SegmentRegisterIndex::SS).m_loaded_in_ss = true;
        updateStackSize();
        break;
    default:
        break;
    }
}

void CPU::_VERR_RM16(Instruction& insn)
{
    if (!getPE() || getVM())
        throw InvalidOpcode("VERR not recognized in real/VM86 mode");

    WORD selector = insn.modrm().read16();
    WORD RPL = selector & 3;
    auto descriptor = getDescriptor(selector);

    if (descriptor.isNull() ||
        descriptor.isOutsideTableLimits() ||
        descriptor.isSystemDescriptor() ||
        !descriptor.asSegmentDescriptor().readable() ||
        (!descriptor.isConformingCode() && (descriptor.DPL() < getCPL() || descriptor.DPL() < RPL)))
    {
        setZF(0);
        return;
    }

    setZF(1);
}

void CPU::_VERW_RM16(Instruction& insn)
{
    if (!getPE() || getVM())
        throw InvalidOpcode("VERW not recognized in real/VM86 mode");

    WORD selector = insn.modrm().read16();
    WORD RPL = selector & 3;
    auto descriptor = getDescriptor(selector);

    if (descriptor.isNull() ||
        descriptor.isOutsideTableLimits() ||
        descriptor.isSystemDescriptor() ||
        descriptor.DPL() < getCPL() ||
        descriptor.DPL() < RPL ||
        !descriptor.asSegmentDescriptor().writable())
    {
        setZF(0);
        return;
    }

    setZF(1);
}

void CPU::_ARPL(Instruction& insn)
{
    if (!getPE() || getVM()) {
        throw InvalidOpcode("ARPL not recognized in real/VM86 mode");
    }
    WORD dest = insn.modrm().read16();
    WORD src = insn.reg16();

    if ((dest & 3) < (src & 3)) {
        setZF(1);
        insn.modrm().write16((dest & ~3) | (src & 3));
    } else {
        setZF(0);
    }
}
