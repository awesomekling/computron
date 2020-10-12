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

#include "Tasking.h"
#include "CPU.h"
#include "debugger.h"

void CPU::_STR_RM16(Instruction& insn)
{
    if (!get_pe() || get_vm()) {
        throw InvalidOpcode("STR not recognized in real/VM86 mode");
    }
    insn.modrm().write_special(m_tr.selector, o32());
}

void CPU::_LTR_RM16(Instruction& insn)
{
    if (!get_pe() || get_vm())
        throw InvalidOpcode("LTR not recognized in real/VM86 mode");

    if (get_cpl() != 0)
        throw GeneralProtectionFault(0, "LTR with CPL != 0");

    u16 selector = insn.modrm().read16();
    auto descriptor = get_descriptor(selector);

    if (descriptor.is_null())
        throw GeneralProtectionFault(0, "LTR with null selector");
    if (!descriptor.is_global())
        throw GeneralProtectionFault(selector & 0xfffc, "LTR selector must reference GDT");
    if (!descriptor.is_tss())
        throw GeneralProtectionFault(selector & 0xfffc, "LTR with non-TSS descriptor");

    auto& tssDescriptor = descriptor.as_tss_descriptor();
    if (tssDescriptor.is_busy())
        throw GeneralProtectionFault(selector & 0xfffc, "LTR with busy TSS");
    if (!tssDescriptor.present())
        throw NotPresent(selector & 0xfffc, "LTR with non-present TSS");

    tssDescriptor.set_busy();
    write_to_gdt(tssDescriptor);

    m_tr.selector = selector;
    m_tr.base = tssDescriptor.base();
    m_tr.limit = tssDescriptor.limit();
    m_tr.is_32bit = tssDescriptor.is_32bit();
#ifdef DEBUG_TASK_SWITCH
    vlog(LogAlert, "LTR { segment: %04x => base:%08x, limit:%08x }", TR.selector, TR.base.get(), TR.limit);
#endif
}

#define EXCEPTION_ON(type, code, condition, reason) \
    do {                                            \
        if ((condition)) {                          \
            throw type(code, reason);               \
        }                                           \
    } while (0)

void CPU::task_switch(u16 task_selector, TSSDescriptor& incomingTSSDescriptor, JumpType source)
{
    ASSERT(incomingTSSDescriptor.is_32bit());

    EXCEPTION_ON(GeneralProtectionFault, 0, incomingTSSDescriptor.is_null(), "Incoming TSS descriptor is null");

    if (!incomingTSSDescriptor.is_global()) {
        if (source == JumpType::IRET)
            throw InvalidTSS(task_selector & 0xfffc, "Incoming TSS descriptor is not from GDT");
        throw GeneralProtectionFault(task_selector & 0xfffc, "Incoming TSS descriptor is not from GDT");
    }
    EXCEPTION_ON(NotPresent, task_selector & 0xfffc, !incomingTSSDescriptor.present(), "Incoming TSS descriptor is not present");

    u32 minimum_tss_limit = incomingTSSDescriptor.is_32bit() ? 108 : 44;
    EXCEPTION_ON(InvalidTSS, task_selector & 0xfffc, incomingTSSDescriptor.limit() < minimum_tss_limit, "Incoming TSS descriptor limit too small");

    if (source == JumpType::IRET) {
        EXCEPTION_ON(InvalidTSS, task_selector & 0xfffc, !incomingTSSDescriptor.is_busy(), "Incoming TSS descriptor is not busy");
    } else {
        EXCEPTION_ON(GeneralProtectionFault, task_selector & 0xfffc, incomingTSSDescriptor.is_busy(), "Incoming TSS descriptor is busy");
    }

    auto outgoingDescriptor = get_descriptor(m_tr.selector);
    if (!outgoingDescriptor.is_tss()) {
        // Hmm, what have we got ourselves into now?
        vlog(LogCPU, "Switching tasks and outgoing TSS is not a TSS:");
        dump_descriptor(outgoingDescriptor);
    }

    TSSDescriptor outgoingTSSDescriptor = outgoingDescriptor.as_tss_descriptor();
    ASSERT(outgoingTSSDescriptor.is_tss());

    if (outgoingTSSDescriptor.base() == incomingTSSDescriptor.base())
        vlog(LogCPU, "Switching to same TSS (%08x)", incomingTSSDescriptor.base().get());

    TSS outgoingTSS(*this, m_tr.base, outgoingTSSDescriptor.is_32bit());

    outgoingTSS.set_eax(get_eax());
    outgoingTSS.set_ebx(get_ebx());
    outgoingTSS.set_ecx(get_ecx());
    outgoingTSS.set_edx(get_edx());
    outgoingTSS.set_ebp(get_ebp());
    outgoingTSS.set_esp(get_esp());
    outgoingTSS.set_esi(get_esi());
    outgoingTSS.set_edi(get_edi());

    if (source == JumpType::JMP || source == JumpType::IRET) {
        outgoingTSSDescriptor.set_available();
        write_to_gdt(outgoingTSSDescriptor);
    }

    u32 outgoingEFlags = get_eflags();

    if (source == JumpType::IRET) {
        outgoingEFlags &= ~Flag::NT;
    }

    outgoingTSS.set_eflags(outgoingEFlags);

    outgoingTSS.set_cs(get_cs());
    outgoingTSS.set_ds(get_ds());
    outgoingTSS.set_es(get_es());
    outgoingTSS.set_fs(get_fs());
    outgoingTSS.set_gs(get_gs());
    outgoingTSS.set_ss(get_ss());
    outgoingTSS.set_ldt(m_ldtr.selector());
    outgoingTSS.set_eip(get_eip());

    if (get_pg())
        outgoingTSS.set_cr3(get_cr3());

    TSS incomingTSS(*this, incomingTSSDescriptor.base(), incomingTSSDescriptor.is_32bit());

#ifdef DEBUG_TASK_SWITCH
    vlog(LogCPU, "Outgoing TSS @ %08x:", outgoingTSSDescriptor.base());
    dumpTSS(outgoingTSS);
    vlog(LogCPU, "Incoming TSS @ %08x:", incomingTSSDescriptor.base());
    dumpTSS(incomingTSS);
#endif

    // First, load all registers from TSS without validating contents.
    m_cr3 = incomingTSS.get_cr3();

    m_ldtr.set_selector(incomingTSS.get_ldt());
    m_ldtr.set_base(LinearAddress());
    m_ldtr.set_limit(0);

    m_cs = incomingTSS.get_cs();
    m_ds = incomingTSS.get_ds();
    m_es = incomingTSS.get_es();
    m_fs = incomingTSS.get_fs();
    m_gs = incomingTSS.get_gs();
    m_ss = incomingTSS.get_ss();

    u32 incomingEFlags = incomingTSS.get_eflags();

    if (incomingEFlags & Flag::VM) {
        vlog(LogCPU, "Incoming task is in VM86 mode, this needs work!");
        ASSERT_NOT_REACHED();
    }

    if (source == JumpType::CALL || source == JumpType::INT) {
        incomingEFlags |= Flag::NT;
    }

    if (incomingTSS.is_32bit())
        set_eflags(incomingEFlags);
    else
        set_flags(incomingEFlags);

    set_eax(incomingTSS.get_eax());
    set_ebx(incomingTSS.get_ebx());
    set_ecx(incomingTSS.get_ecx());
    set_edx(incomingTSS.get_edx());
    set_ebp(incomingTSS.get_ebp());
    set_esp(incomingTSS.get_esp());
    set_esi(incomingTSS.get_esi());
    set_edi(incomingTSS.get_edi());

    if (source == JumpType::CALL || source == JumpType::INT) {
        incomingTSS.set_backlink(m_tr.selector);
    }

    m_tr.selector = incomingTSSDescriptor.index();
    m_tr.base = incomingTSSDescriptor.base();
    m_tr.limit = incomingTSSDescriptor.limit();
    m_tr.is_32bit = incomingTSSDescriptor.is_32bit();

    if (source != JumpType::IRET) {
        incomingTSSDescriptor.set_busy();
        write_to_gdt(incomingTSSDescriptor);
    }

    m_cr0 |= CR0::TS; // Task Switched

    // Now, let's validate!
    auto ldtDescriptor = get_descriptor(m_ldtr.selector());
    if (!ldtDescriptor.is_null()) {
        if (!ldtDescriptor.is_global())
            throw InvalidTSS(m_ldtr.selector() & 0xfffc, "Incoming LDT is not in GDT");
        if (!ldtDescriptor.is_ldt())
            throw InvalidTSS(m_ldtr.selector() & 0xfffc, "Incoming LDT is not an LDT");
    }

    unsigned incoming_cpl = get_cs() & 3;

    auto cs_descriptor = get_descriptor(m_cs);
    if (cs_descriptor.is_code()) {
        if (cs_descriptor.is_nonconforming_code()) {
            if (cs_descriptor.dpl() != (get_cs() & 3))
                throw InvalidTSS(get_cs() & 0xfffc, QString("CS is non-conforming with DPL(%1) != RPL(%2)").arg(cs_descriptor.dpl()).arg(get_cs() & 3));
        } else if (cs_descriptor.is_conforming_code()) {
            if (cs_descriptor.dpl() > (get_cs() & 3))
                throw InvalidTSS(get_cs() & 0xfffc, "CS is conforming with DPL > RPL");
        }
    }
    auto ss_descriptor = get_descriptor(get_ss());
    if (!ss_descriptor.is_null()) {
        if (ss_descriptor.is_outside_table_limits())
            throw InvalidTSS(get_ss() & 0xfffc, "SS outside table limits");
        if (!ss_descriptor.is_data())
            throw InvalidTSS(get_ss() & 0xfffc, "SS is not a data segment");
        if (!ss_descriptor.as_data_segment_descriptor().writable())
            throw InvalidTSS(get_ss() & 0xfffc, "SS is not writable");
        if (!ss_descriptor.present())
            throw StackFault(get_ss() & 0xfffc, "SS is not present");
        if (ss_descriptor.dpl() != incoming_cpl)
            throw InvalidTSS(get_ss() & 0xfffc, QString("SS DPL(%1) != CPL(%2)").arg(ss_descriptor.dpl()).arg(incoming_cpl));
    }

    if (!ldtDescriptor.is_null()) {
        if (!ldtDescriptor.present())
            throw InvalidTSS(m_ldtr.selector() & 0xfffc, "Incoming LDT is not present");
    }

    if (!cs_descriptor.is_code())
        throw InvalidTSS(get_cs() & 0xfffc, "CS is not a code segment");
    if (!cs_descriptor.present())
        throw InvalidTSS(get_cs() & 0xfffc, "CS is not present");

    if (ss_descriptor.dpl() != (get_ss() & 3))
        throw InvalidTSS(get_ss() & 0xfffc, "SS DPL != RPL");

    auto validate_data_segment = [&](SegmentRegisterIndex segreg) {
        u16 selector = read_segment_register(segreg);
        auto descriptor = get_descriptor(selector);
        if (descriptor.is_null())
            return;
        if (descriptor.is_outside_table_limits())
            throw InvalidTSS(selector & 0xfffc, "DS/ES/FS/GS outside table limits");
        if (!descriptor.is_segment_descriptor())
            throw InvalidTSS(selector & 0xfffc, "DS/ES/FS/GS is a system segment");
        if (!descriptor.present())
            throw NotPresent(selector & 0xfffc, "DS/ES/FS/GS is not present");
        if (!descriptor.is_conforming_code() && descriptor.dpl() < incoming_cpl)
            throw InvalidTSS(selector & 0xfffc, "DS/ES/FS/GS has DPL < CPL and is not a conforming code segment");
    };

    validate_data_segment(SegmentRegisterIndex::DS);
    validate_data_segment(SegmentRegisterIndex::ES);
    validate_data_segment(SegmentRegisterIndex::FS);
    validate_data_segment(SegmentRegisterIndex::GS);

    EXCEPTION_ON(GeneralProtectionFault, 0, get_eip() > cached_descriptor(SegmentRegisterIndex::CS).effective_limit(), "Task switch to EIP outside CS limit");

    set_ldt(incomingTSS.get_ldt());
    set_cs(incomingTSS.get_cs());
    set_es(incomingTSS.get_es());
    set_ds(incomingTSS.get_ds());
    set_fs(incomingTSS.get_fs());
    set_gs(incomingTSS.get_gs());
    set_ss(incomingTSS.get_ss());
    set_eip(incomingTSS.get_eip());

    if (get_tf()) {
        vlog(LogCPU, "Leaving task switch with TF=1");
    }

    if (get_vm()) {
        vlog(LogCPU, "Leaving task switch with VM=1");
    }

#ifdef DEBUG_TASK_SWITCH
    vlog(LogCPU, "Task switched to %08x, cpl=%u, iopl=%u", incomingTSSDescriptor.base(), getCPL(), getIOPL());
#endif
}

void CPU::dump_tss(const TSS& tss)
{
    vlog(LogCPU, "TSS bits=%u", tss.is_32bit() ? 32 : 16);
    vlog(LogCPU, "eax=%08x ebx=%08x ecx=%08x edx=%08x", tss.get_eax(), tss.get_ebx(), tss.get_ecx(), tss.get_edx());
    vlog(LogCPU, "esi=%08x edi=%08x ebp=%08x esp=%08x", tss.get_esi(), tss.get_edi(), tss.get_ebp(), tss.get_esp());
    vlog(LogCPU, "ldt=%04x backlink=%04x cr3=%08x", tss.get_ldt(), tss.get_backlink(), get_pg() ? tss.get_cr3() : 0);
    vlog(LogCPU, "ds=%04x ss=%04x es=%04x fs=%04x gs=%04x", tss.get_ds(), tss.get_ss(), tss.get_es(), tss.get_fs(), tss.get_gs());
    vlog(LogCPU, "cs=%04x eip=%08x eflags=%08x", tss.get_cs(), tss.get_eip(), tss.get_eflags());
    vlog(LogCPU, "stack0 { %04x:%08x }", tss.get_ss0(), tss.get_esp0());
    vlog(LogCPU, "stack1 { %04x:%08x }", tss.get_ss1(), tss.get_esp1());
    vlog(LogCPU, "stack2 { %04x:%08x }", tss.get_ss2(), tss.get_esp2());
}

void CPU::task_switch(u16 task_selector, JumpType source)
{
    auto descriptor = get_descriptor(task_selector);
    auto& tssDescriptor = descriptor.as_tss_descriptor();
    task_switch(task_selector, tssDescriptor, source);
}

TSS CPU::current_tss()
{
    return TSS(*this, m_tr.base, m_tr.is_32bit);
}

struct TSS32 {
    u16 backlink, __blh;
    u32 esp0;
    u16 ss0, __ss0h;
    u32 esp1;
    u16 ss1, __ss1h;
    u32 esp2;
    u16 ss2, __ss2h;
    u32 cr3, eip, eflags;
    u32 eax, ecx, edx, ebx, esp, ebp, esi, edi;
    u16 es, __esh;
    u16 cs, __csh;
    u16 ss, __ssh;
    u16 ds, __dsh;
    u16 fs, __fsh;
    u16 gs, __gsh;
    u16 ldt, __ldth;
    u16 trace, iomapbase;
} __attribute__((packed));

struct TSS16 {
    u16 backlink;
    u16 sp0;
    u16 ss0;
    u16 sp1;
    u16 ss1;
    u16 sp2;
    u16 ss2;
    u16 ip;
    u16 flags;
    u16 ax, cx, dx, bx, sp, bp, si, di;
    u16 es;
    u16 cs;
    u16 ss;
    u16 ds;
    u16 fs;
    u16 gs;
    u16 ldt;
} __attribute__((packed));

TSS::TSS(CPU& cpu, LinearAddress base, bool is32Bit)
    : m_cpu(cpu)
    , m_base(base)
    , m_is_32bit(is32Bit)
{
}

#define TSS_FIELD_16(name)                                                           \
    void TSS::set_##name(u16 value)                                                  \
    {                                                                                \
        if (m_is_32bit)                                                              \
            m_cpu.write_memory_metal16(m_base.offset(offsetof(TSS32, name)), value); \
        else                                                                         \
            m_cpu.write_memory_metal16(m_base.offset(offsetof(TSS16, name)), value); \
    }                                                                                \
    u16 TSS::get_##name() const                                                      \
    {                                                                                \
        if (m_is_32bit)                                                              \
            return m_cpu.read_memory_metal16(m_base.offset(offsetof(TSS32, name)));  \
        return m_cpu.read_memory_metal16(m_base.offset(offsetof(TSS16, name)));      \
    }

#define TSS_FIELD_16OR32(name)                                                          \
    u32 TSS::get_e##name() const                                                        \
    {                                                                                   \
        if (m_is_32bit)                                                                 \
            return m_cpu.read_memory_metal32(m_base.offset(offsetof(TSS32, e##name)));  \
        return m_cpu.read_memory_metal16(m_base.offset(offsetof(TSS16, name)));         \
    }                                                                                   \
    void TSS::set_e##name(u32 value)                                                    \
    {                                                                                   \
        if (m_is_32bit)                                                                 \
            m_cpu.write_memory_metal32(m_base.offset(offsetof(TSS32, e##name)), value); \
        else                                                                            \
            m_cpu.write_memory_metal16(m_base.offset(offsetof(TSS16, name)), value);    \
    }

u32 TSS::get_cr3() const
{
    ASSERT(m_is_32bit);
    return m_cpu.read_memory_metal32(m_base.offset(offsetof(TSS32, cr3)));
}

void TSS::set_cr3(u32 value)
{
    ASSERT(m_is_32bit);
    m_cpu.write_memory_metal32(m_base.offset(offsetof(TSS32, cr3)), value);
}

TSS_FIELD_16OR32(ax)
TSS_FIELD_16OR32(bx)
TSS_FIELD_16OR32(cx)
TSS_FIELD_16OR32(dx)
TSS_FIELD_16OR32(si)
TSS_FIELD_16OR32(di)
TSS_FIELD_16OR32(bp)
TSS_FIELD_16OR32(sp)
TSS_FIELD_16OR32(ip)
TSS_FIELD_16OR32(sp0)
TSS_FIELD_16OR32(sp1)
TSS_FIELD_16OR32(sp2)
TSS_FIELD_16OR32(flags)

TSS_FIELD_16(backlink)
TSS_FIELD_16(ldt)
TSS_FIELD_16(cs)
TSS_FIELD_16(ds)
TSS_FIELD_16(es)
TSS_FIELD_16(ss)
TSS_FIELD_16(fs)
TSS_FIELD_16(gs)
TSS_FIELD_16(ss0)
TSS_FIELD_16(ss1)
TSS_FIELD_16(ss2)

u32 TSS::get_ring_esp(u8 ring) const
{
    if (ring == 0)
        return get_esp0();
    if (ring == 1)
        return get_esp1();
    if (ring == 2)
        return get_esp2();
    ASSERT_NOT_REACHED();
    return 0;
}

u16 TSS::get_ring_ss(u8 ring) const
{
    if (ring == 0)
        return get_ss0();
    if (ring == 1)
        return get_ss1();
    if (ring == 2)
        return get_ss2();
    ASSERT_NOT_REACHED();
    return 0;
}

u16 TSS::get_io_map_base() const
{
    ASSERT(m_is_32bit);
    return m_cpu.read_memory_metal16(m_base.offset(offsetof(TSS32, iomapbase)));
}
