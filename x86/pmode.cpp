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
    if (insn.modrm().is_register())
        throw InvalidOpcode(QString("%1 with register destination").arg(insn.mnemonic()));

    snoop(insn.modrm().segment(), insn.modrm().offset(), MemoryAccessType::Write);
    snoop(insn.modrm().segment(), insn.modrm().offset() + 6, MemoryAccessType::Write);
    u32 maskedBase = o16() ? (table.base().get() & 0x00ffffff) : table.base().get();
    write_memory16(insn.modrm().segment(), insn.modrm().offset(), table.limit());
    write_memory32(insn.modrm().segment(), insn.modrm().offset() + 2, maskedBase);
}

void CPU::_SGDT(Instruction& insn)
{
    doSGDTorSIDT(insn, m_gdtr);
}

void CPU::_SIDT(Instruction& insn)
{
    doSGDTorSIDT(insn, m_idtr);
}

void CPU::_SLDT_RM16(Instruction& insn)
{
    if (!get_pe() || get_vm()) {
        throw InvalidOpcode("SLDT not recognized in real/VM86 mode");
    }
    insn.modrm().write_special(m_ldtr.selector(), o32());
}

void CPU::set_ldt(u16 selector)
{
    auto descriptor = get_descriptor(selector);
    LinearAddress base;
    u16 limit = 0;
    if (!descriptor.is_null()) {
        if (descriptor.is_ldt()) {
            auto& ldtDescriptor = descriptor.as_ldt_descriptor();
            if (!descriptor.present()) {
                throw NotPresent(selector & 0xfffc, "LDT segment not present");
            }
            base = ldtDescriptor.base();
            limit = ldtDescriptor.limit();
        } else {
            throw GeneralProtectionFault(selector & 0xfffc, "Not an LDT descriptor");
        }
    }

    m_ldtr.set_selector(selector);
    m_ldtr.set_base(base);
    m_ldtr.set_limit(limit);

#ifdef DEBUG_DESCRIPTOR_TABLES
    vlog(LogAlert, "setLDT { segment: %04X => base:%08X, limit:%08X }", m_LDTR.selector(), m_LDTR.base(), m_LDTR.limit());
#endif
}

void CPU::_LLDT_RM16(Instruction& insn)
{
    if (!get_pe() || get_vm()) {
        throw InvalidOpcode("LLDT not recognized in real/VM86 mode");
    }

    if (get_cpl() != 0)
        throw GeneralProtectionFault(0, "LLDT with CPL != 0");

    set_ldt(insn.modrm().read16());
#ifdef DEBUG_DESCRIPTOR_TABLES
    dumpLDT();
#endif
}

void CPU::dump_ldt()
{
    for (unsigned i = 0; i < m_ldtr.limit(); i += 8) {
        dump_descriptor(get_descriptor(i | 4));
    }
}

void CPU::doLGDTorLIDT(Instruction& insn, DescriptorTableRegister& table)
{
    if (insn.modrm().is_register())
        throw InvalidOpcode(QString("%1 with register source").arg(insn.mnemonic()));

    if (get_cpl() != 0)
        throw GeneralProtectionFault(0, QString("%1 with CPL != 0").arg(insn.mnemonic()));

    u32 base = read_memory32(insn.modrm().segment(), insn.modrm().offset() + 2);
    u16 limit = read_memory16(insn.modrm().segment(), insn.modrm().offset());
    u32 baseMask = o32() ? 0xffffffff : 0x00ffffff;
    table.set_base(LinearAddress(base & baseMask));
    table.set_limit(limit);
}

void CPU::_LGDT(Instruction& insn)
{
    doLGDTorLIDT(insn, m_gdtr);
#ifdef DEBUG_DESCRIPTOR_TABLES
    vlog(LogAlert, "LGDT { base:%08X, limit:%08X }", m_GDTR.base().get(), m_GDTR.limit());
    dumpGDT();
#endif
}

void CPU::_LIDT(Instruction& insn)
{
    doLGDTorLIDT(insn, m_idtr);
#if DEBUG_DESCRIPTOR_TABLES
    dumpIDT();
#endif
}

void CPU::dump_gdt()
{
    vlog(LogDump, "GDT { base:%08x, limit:%08x }", m_gdtr.base().get(), m_gdtr.limit());
    for (unsigned i = 0; i < m_gdtr.limit(); i += 8) {
        dump_descriptor(get_descriptor(i));
    }
}

void CPU::dump_idt()
{
    vlog(LogDump, "IDT { base:%08X, limit:%08X }", m_idtr.base().get(), m_idtr.limit());
    if (get_pe()) {
        for (u32 isr = 0; isr < (m_idtr.limit() / 16); ++isr) {
            dump_descriptor(get_interrupt_descriptor(isr));
        }
    }
}

void CPU::_CLTS(Instruction&)
{
    if (get_pe()) {
        if (get_cpl() != 0) {
            throw GeneralProtectionFault(0, QString("CLTS with CPL!=0(%1)").arg(get_cpl()));
        }
    }
    m_cr0 &= ~(1 << 3);
}

void CPU::_LMSW_RM16(Instruction& insn)
{
    if (get_pe()) {
        if (get_cpl() != 0) {
            throw GeneralProtectionFault(0, QString("LMSW with CPL!=0(%1)").arg(get_cpl()));
        }
    }

    u16 msw = insn.modrm().read16();

    if (get_pe()) {
        // LMSW cannot exit protected mode.
        msw |= CR0::PE;
    }

    m_cr0 = (m_cr0 & 0xFFFFFFF0) | (msw & 0x0F);
#ifdef PMODE_DEBUG
    vlog(LogCPU, "LMSW set CR0=%08X, PE=%u", getCR0(), get_pe());
#endif
}

void CPU::_SMSW_RM16(Instruction& insn)
{
#ifdef PMODE_DEBUG
    vlog(LogCPU, "SMSW get LSW(CR0)=%04X, PE=%u", getCR0() & 0xFFFF, get_pe());
#endif
    insn.modrm().write_special(get_cr0(), o32());
}

void CPU::_LAR_reg16_RM16(Instruction& insn)
{
    if (!get_pe() || get_vm())
        throw InvalidOpcode("LAR not recognized in real/VM86 mode");

    // FIXME: This has various ways it can fail, implement them.
    u16 selector = insn.modrm().read16() & 0xffff;
    u16 selectorRPL = selector & 3;
    auto descriptor = get_descriptor(selector);
    if (descriptor.is_null() || descriptor.is_outside_table_limits() || descriptor.dpl() < get_cpl() || descriptor.dpl() < selectorRPL) {
        set_zf(0);
        return;
    }
    insn.reg16() = descriptor.m_high & 0xff00;
    set_zf(1);
}

void CPU::_LAR_reg32_RM32(Instruction& insn)
{
    if (!get_pe() || get_vm())
        throw InvalidOpcode("LAR not recognized in real/VM86 mode");

    // FIXME: This has various ways it can fail, implement them.
    u16 selector = insn.modrm().read32() & 0xffff;
    u16 selectorRPL = selector & 3;
    auto descriptor = get_descriptor(selector);
    if (descriptor.is_null() || descriptor.is_outside_table_limits() || descriptor.dpl() < get_cpl() || descriptor.dpl() < selectorRPL) {
        set_zf(0);
        return;
    }
    insn.reg32() = descriptor.m_high & 0x00ffff00;
    set_zf(1);
}

static bool isValidDescriptorForLSL(const Descriptor& descriptor)
{
    if (descriptor.is_null())
        return true;
    if (descriptor.is_outside_table_limits())
        return true;
    if (descriptor.is_segment_descriptor())
        return true;

    switch (descriptor.as_system_descriptor().type()) {
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
    if (!get_pe() || get_vm())
        throw InvalidOpcode("LSL not recognized in real/VM86 mode");

    u16 selector = insn.modrm().read16() & 0xffff;
    auto descriptor = get_descriptor(selector);
    // FIXME: This should also fail for conforming code segments somehow.
    if (!isValidDescriptorForLSL(descriptor)) {
        set_zf(0);
        return;
    }

    u32 effectiveLimit;
    if (descriptor.is_ldt())
        effectiveLimit = descriptor.as_ldt_descriptor().effective_limit();
    else if (descriptor.is_tss())
        effectiveLimit = descriptor.as_tss_descriptor().effective_limit();
    else
        effectiveLimit = descriptor.as_segment_descriptor().effective_limit();
    insn.reg16() = effectiveLimit;
    set_zf(1);
}

void CPU::_LSL_reg32_RM32(Instruction& insn)
{
    if (!get_pe() || get_vm())
        throw InvalidOpcode("LSL not recognized in real/VM86 mode");

    u16 selector = insn.modrm().read16() & 0xffff;
    auto descriptor = get_descriptor(selector);
    // FIXME: This should also fail for conforming code segments somehow.
    if (descriptor.is_outside_table_limits()) {
        set_zf(0);
        return;
    }
    u32 effectiveLimit;
    if (descriptor.is_ldt())
        effectiveLimit = descriptor.as_ldt_descriptor().effective_limit();
    else if (descriptor.is_tss())
        effectiveLimit = descriptor.as_tss_descriptor().effective_limit();
    else
        effectiveLimit = descriptor.as_segment_descriptor().effective_limit();
    insn.reg32() = effectiveLimit;
    set_zf(1);
}

const char* toString(SegmentRegisterIndex segment)
{
    switch (segment) {
    case SegmentRegisterIndex::CS:
        return "CS";
    case SegmentRegisterIndex::DS:
        return "DS";
    case SegmentRegisterIndex::ES:
        return "ES";
    case SegmentRegisterIndex::SS:
        return "SS";
    case SegmentRegisterIndex::FS:
        return "FS";
    case SegmentRegisterIndex::GS:
        return "GS";
    default:
        break;
    }
    return nullptr;
}

void CPU::raise_exception(const Exception& e)
{
    if (options.crash_on_exception) {
        dump_all();
        vlog(LogAlert, "CRASH ON EXCEPTION");
        ASSERT_NOT_REACHED();
    }

    try {
        set_eip(current_base_instruction_pointer());
        if (e.has_code()) {
            interrupt(e.num(), InterruptSource::External, e.code());
        } else {
            interrupt(e.num(), InterruptSource::External);
        }
    } catch (Exception e) {
        ASSERT_NOT_REACHED();
    }
}

Exception CPU::GeneralProtectionFault(u16 code, const QString& reason)
{
    u16 selector = code & 0xfff8;
    bool TI = code & 4;
    bool I = code & 2;
    bool EX = code & 1;

    if (options.log_exceptions)
        vlog(LogCPU, "Exception: #GP(%04x) selector=%04X, TI=%u, I=%u, EX=%u :: %s", code, selector, TI, I, EX, qPrintable(reason));
    if (options.crash_on_general_protection_fault) {
        dump_all();
        vlog(LogAlert, "CRASH ON GPF");
        ASSERT_NOT_REACHED();
    }
    return Exception(0xd, code, reason);
}

Exception CPU::StackFault(u16 selector, const QString& reason)
{
    if (options.log_exceptions)
        vlog(LogCPU, "Exception: #SS(%04x) :: %s", selector, qPrintable(reason));
    return Exception(0xc, selector, reason);
}

Exception CPU::NotPresent(u16 selector, const QString& reason)
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

Exception CPU::InvalidTSS(u16 selector, const QString& reason)
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

void CPU::validate_segment_load(SegmentRegisterIndex reg, u16 selector, const Descriptor& descriptor)
{
    if (!get_pe() || get_vm())
        return;

    u8 selectorRPL = selector & 3;

    if (descriptor.is_outside_table_limits()) {
        throw GeneralProtectionFault(selector & 0xfffc, "Selector outside table limits");
    }

    if (reg == SegmentRegisterIndex::SS) {
        if (descriptor.is_null()) {
            throw GeneralProtectionFault(0, "ss loaded with null descriptor");
        }
        if (selectorRPL != get_cpl()) {
            throw GeneralProtectionFault(selector & 0xfffc, QString("ss selector RPL(%1) != CPL(%2)").arg(selectorRPL).arg(get_cpl()));
        }
        if (!descriptor.is_data() || !descriptor.as_data_segment_descriptor().writable()) {
            throw GeneralProtectionFault(selector & 0xfffc, "ss loaded with something other than a writable data segment");
        }
        if (descriptor.dpl() != get_cpl()) {
            throw GeneralProtectionFault(selector & 0xfffc, QString("ss selector leads to descriptor with DPL(%1) != CPL(%2)").arg(descriptor.dpl()).arg(get_cpl()));
        }
        if (!descriptor.present()) {
            throw StackFault(selector & 0xfffc, "ss loaded with non-present segment");
        }
        return;
    }

    if (descriptor.is_null())
        return;

    if (reg == SegmentRegisterIndex::DS
        || reg == SegmentRegisterIndex::ES
        || reg == SegmentRegisterIndex::FS
        || reg == SegmentRegisterIndex::GS) {
        if (!descriptor.is_data() && (descriptor.is_code() && !descriptor.as_code_segment_descriptor().readable())) {
            throw GeneralProtectionFault(selector & 0xfffc, QString("%1 loaded with non-data or non-readable code segment").arg(register_name(reg)));
        }
        if (descriptor.is_data() || descriptor.is_nonconforming_code()) {
            if (selectorRPL > descriptor.dpl()) {
                throw GeneralProtectionFault(selector & 0xfffc, QString("%1 loaded with data or non-conforming code segment and RPL > DPL").arg(register_name(reg)));
            }
            if (get_cpl() > descriptor.dpl()) {
                throw GeneralProtectionFault(selector & 0xfffc, QString("%1 loaded with data or non-conforming code segment and CPL > DPL").arg(register_name(reg)));
            }
        }
        if (!descriptor.present()) {
            throw NotPresent(selector & 0xfffc, QString("%1 loaded with non-present segment").arg(register_name(reg)));
        }
    }

    if (!descriptor.is_null() && !descriptor.is_segment_descriptor()) {
        dump_descriptor(descriptor);
        throw GeneralProtectionFault(selector & 0xfffc, QString("%1 loaded with system segment").arg(register_name(reg)));
    }
}

void CPU::write_segment_register(SegmentRegisterIndex segreg, u16 selector)
{
    if ((int)segreg >= 6) {
        throw InvalidOpcode("Write to invalid segment register");
    }

    Descriptor descriptor;
    if (!get_pe() || get_vm())
        descriptor = get_real_mode_or_vm86_descriptor(selector, segreg);
    else
        descriptor = get_descriptor(selector);

    validate_segment_load(segreg, selector, descriptor);

    *m_segment_map[(int)segreg] = selector;

    if (descriptor.is_null()) {
        cached_descriptor(segreg) = descriptor.as_segment_descriptor();
        return;
    }

    ASSERT(descriptor.is_segment_descriptor());
    cached_descriptor(segreg) = descriptor.as_segment_descriptor();
    if (options.pedebug) {
        if (get_pe()) {
            vlog(LogCPU, "%s loaded with %04x { type:%02X, base:%08X, limit:%08X }",
                toString(segreg),
                selector,
                descriptor.as_segment_descriptor().type(),
                descriptor.as_segment_descriptor().base(),
                descriptor.as_segment_descriptor().limit());
        }
    }

    switch (segreg) {
    case SegmentRegisterIndex::CS:
        if (get_pe()) {
            if (get_vm())
                set_cpl(3);
            else
                set_cpl(descriptor.dpl());
        }
        update_default_sizes();
        update_code_segment_cache();
        break;
    case SegmentRegisterIndex::SS:
        cached_descriptor(SegmentRegisterIndex::SS).m_loaded_in_ss = true;
        update_stack_size();
        break;
    default:
        break;
    }
}

void CPU::_VERR_RM16(Instruction& insn)
{
    if (!get_pe() || get_vm())
        throw InvalidOpcode("VERR not recognized in real/VM86 mode");

    u16 selector = insn.modrm().read16();
    u16 RPL = selector & 3;
    auto descriptor = get_descriptor(selector);

    if (descriptor.is_null() || descriptor.is_outside_table_limits() || descriptor.is_system_descriptor() || !descriptor.as_segment_descriptor().readable() || (!descriptor.is_conforming_code() && (descriptor.dpl() < get_cpl() || descriptor.dpl() < RPL))) {
        set_zf(0);
        return;
    }

    set_zf(1);
}

void CPU::_VERW_RM16(Instruction& insn)
{
    if (!get_pe() || get_vm())
        throw InvalidOpcode("VERW not recognized in real/VM86 mode");

    u16 selector = insn.modrm().read16();
    u16 RPL = selector & 3;
    auto descriptor = get_descriptor(selector);

    if (descriptor.is_null() || descriptor.is_outside_table_limits() || descriptor.is_system_descriptor() || descriptor.dpl() < get_cpl() || descriptor.dpl() < RPL || !descriptor.as_segment_descriptor().writable()) {
        set_zf(0);
        return;
    }

    set_zf(1);
}

void CPU::_ARPL(Instruction& insn)
{
    if (!get_pe() || get_vm()) {
        throw InvalidOpcode("ARPL not recognized in real/VM86 mode");
    }
    u16 dest = insn.modrm().read16();
    u16 src = insn.reg16();

    if ((dest & 3) < (src & 3)) {
        set_zf(1);
        insn.modrm().write16((dest & ~3) | (src & 3));
    } else {
        set_zf(0);
    }
}
