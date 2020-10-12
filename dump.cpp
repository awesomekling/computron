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
#include <stdio.h>

unsigned CPU::dump_disassembled_internal(SegmentDescriptor& descriptor, u32 offset)
{
    char buf[512];
    char* p = buf;
    const u8* data = nullptr;

    try {
        data = memory_pointer(descriptor, offset);
    } catch (...) {
        data = nullptr;
    }

    if (!data) {
        vlog(LogCPU, "dumpDisassembled can't dump %04x:%08x", descriptor.index(), offset);
        return 0;
    }

    SimpleInstructionStream stream(data);
    auto insn = Instruction::from_stream(stream, m_operand_size32, m_address_size32);

    if (x32())
        p += sprintf(p, "%04x:%08x ", descriptor.index(), offset);
    else
        p += sprintf(p, "%04x:%04x ", descriptor.index(), offset);

    for (unsigned i = 0; i < insn.length(); ++i)
        p += sprintf(p, "%02x", data[i]);

    for (unsigned i = 0; i < (32 - (insn.length() * 2)); ++i)
        p += sprintf(p, " ");

    if (insn.is_valid())
        p += sprintf(p, " %s", qPrintable(insn.to_string(offset, x32())));
    else
        p += sprintf(p, " <invalid instruction>");

    vlog(LogDump, buf);
    return insn.length();
}

unsigned CPU::dump_disassembled(SegmentDescriptor& descriptor, u32 offset, unsigned count)
{
    unsigned bytes = 0;
    for (unsigned i = 0; i < count; ++i) {
        bytes += dump_disassembled_internal(descriptor, offset + bytes);
    }
    return bytes;
}

unsigned CPU::dump_disassembled(LogicalAddress address, unsigned count)
{
    auto descriptor = get_segment_descriptor(address.selector());
    return dump_disassembled(descriptor, address.offset(), count);
}

#ifdef CT_TRACE
void CPU::dump_trace()
{
#    if 0
    fprintf(stderr,
        "%04X:%08X "
        "EAX=%08X EBX=%08X ECX=%08X EDX=%08X ESP=%08X EBP=%08X ESI=%08X EDI=%08X "
        "CR0=%08X CR1=%08X CR2=%08X CR3=%08X CR4=%08X CR5=%08X CR6=%08X CR7=%08X "
        "DR0=%08X DR1=%08X DR2=%08X DR3=%08X DR4=%08X DR5=%08X DR6=%08X DR7=%08X "
        "DS=%04X ES=%04X SS=%04X FS=%04X GS=%04X "
        "C=%u P=%u A=%u Z=%u S=%u I=%u D=%u O=%u\n",
        getCS(), getEIP(),
        get_eax(), get_ebx(), get_ecx(), get_edx(), get_esp(), get_ebp(), get_esi(), get_edi(),
        getCR0(), getCR1(), getCR2(), getCR3(), getCR4(), getCR5(), getCR6(), getCR7(),
        getDR0(), getDR1(), getDR2(), getDR3(), getDR4(), getDR5(), getDR6(), getDR7(),
        getDS(), getES(), get_ss(), getFS(), getGS(),
        getCF(), getPF(), getAF(), getZF(),
        getSF(), getIF(), getDF(), getOF()
    );
#    else
    printf(
        "%04X:%08X %02X "
        "EAX=%08X EBX=%08X ECX=%08X EDX=%08X ESP=%08X EBP=%08X ESI=%08X EDI=%08X "
        "CR0=%08X CR3=%08X CPL=%u IOPL=%u A20=%u "
        "DS=%04X ES=%04X SS=%04X FS=%04X GS=%04X "
        "C=%u P=%u A=%u Z=%u S=%u I=%u D=%u O=%u "
        "NT=%u VM=%u "
        "A%u O%u X%u S%u\n",
        get_cs(), get_eip(),
        read_memory8(SegmentRegisterIndex::CS, get_eip()),
        get_eax(), get_ebx(), get_ecx(), get_edx(), get_esp(), get_ebp(), get_esi(), get_edi(),
        get_cr0(), get_cr3(), get_cpl(), get_iopl(),
        is_a20_enabled(),
        get_ds(), get_es(), get_ss(), get_fs(), get_gs(),
        get_cf(), get_pf(), get_af(), get_zf(),
        get_sf(), get_if(), get_df(), get_of(),
        get_nt(), get_vm(),
        a16() ? 16 : 32,
        o16() ? 16 : 32,
        x16() ? 16 : 32,
        s16() ? 16 : 32);
#    endif
}
#endif

void CPU::dump_selector(const char* prefix, SegmentRegisterIndex segreg)
{
    auto& descriptor = cached_descriptor(segreg);
    if (descriptor.is_null())
        vlog(LogDump, "%s: %04x: (null descriptor)", prefix, read_segment_register(segreg));
    else
        dump_descriptor(descriptor, prefix);
}

const char* CPU::register_name(SegmentRegisterIndex index)
{
    switch (index) {
    case SegmentRegisterIndex::CS:
        return "cs";
    case SegmentRegisterIndex::DS:
        return "ds";
    case SegmentRegisterIndex::ES:
        return "es";
    case SegmentRegisterIndex::SS:
        return "ss";
    case SegmentRegisterIndex::FS:
        return "fs";
    case SegmentRegisterIndex::GS:
        return "gs";
    default:
        ASSERT_NOT_REACHED();
        return nullptr;
    }
}

const char* CPU::register_name(CPU::RegisterIndex8 register_index)
{
    switch (register_index) {
    case CPU::RegisterAL:
        return "al";
    case CPU::RegisterBL:
        return "bl";
    case CPU::RegisterCL:
        return "cl";
    case CPU::RegisterDL:
        return "dl";
    case CPU::RegisterAH:
        return "ah";
    case CPU::RegisterBH:
        return "bh";
    case CPU::RegisterCH:
        return "ch";
    case CPU::RegisterDH:
        return "dh";
    }
    ASSERT_NOT_REACHED();
    return nullptr;
}

const char* CPU::register_name(CPU::RegisterIndex16 register_index)
{
    switch (register_index) {
    case CPU::RegisterAX:
        return "ax";
    case CPU::RegisterBX:
        return "bx";
    case CPU::RegisterCX:
        return "cx";
    case CPU::RegisterDX:
        return "dx";
    case CPU::RegisterBP:
        return "bp";
    case CPU::RegisterSP:
        return "sp";
    case CPU::RegisterSI:
        return "si";
    case CPU::RegisterDI:
        return "di";
    }
    ASSERT_NOT_REACHED();
    return nullptr;
}

const char* CPU::register_name(CPU::RegisterIndex32 register_index)
{
    switch (register_index) {
    case CPU::RegisterEAX:
        return "eax";
    case CPU::RegisterEBX:
        return "ebx";
    case CPU::RegisterECX:
        return "ecx";
    case CPU::RegisterEDX:
        return "edx";
    case CPU::RegisterEBP:
        return "ebp";
    case CPU::RegisterESP:
        return "esp";
    case CPU::RegisterESI:
        return "esi";
    case CPU::RegisterEDI:
        return "edi";
    }
    ASSERT_NOT_REACHED();
    return nullptr;
}

void CPU::dump_watches()
{
    for (WatchedAddress& watch : m_watches) {
        if (watch.size == ByteSize) {
            auto data = read_physical_memory<u8>(watch.address);
            if (data != watch.lastSeenValue) {
                vlog(LogDump, "\033[32;1m%08X\033[0m [%-16s] %02X", watch.address, qPrintable(watch.name), data);
                watch.lastSeenValue = data;
                if (cycle() > 1 && watch.breakOnChange)
                    debugger().enter();
            }
        } else if (watch.size == WordSize) {
            auto data = read_physical_memory<u16>(watch.address);
            if (data != watch.lastSeenValue) {
                vlog(LogDump, "\033[32;1m%08X\033[0m [%-16s] %04X", watch.address, qPrintable(watch.name), data);
                watch.lastSeenValue = data;
                if (cycle() > 1 && watch.breakOnChange)
                    debugger().enter();
            }
        } else if (watch.size == DWordSize) {
            auto data = read_physical_memory<u32>(watch.address);
            if (data != watch.lastSeenValue) {
                vlog(LogDump, "\033[32;1m%08X\033[0m [%-16s] %08X", watch.address, qPrintable(watch.name), data);
                watch.lastSeenValue = data;
                if (cycle() > 1 && watch.breakOnChange)
                    debugger().enter();
            }
        }
    }
}

void CPU::dump_all()
{
    if (get_pe() && m_tr.selector != 0) {
        auto descriptor = get_descriptor(m_tr.selector);
        if (descriptor.is_tss()) {
            auto& tssDescriptor = descriptor.as_tss_descriptor();
            TSS tss(*this, tssDescriptor.base(), tssDescriptor.is_32bit());
            dump_tss(tss);
        }
    }

    vlog(LogDump, "eax: %08x  ebx: %08x  ecx: %08x  edx: %08x", get_eax(), get_ebx(), get_ecx(), get_edx());
    vlog(LogDump, "ebp: %08x  esp: %08x  esi: %08x  edi: %08x", get_ebp(), get_esp(), get_esi(), get_edi());

    if (!get_pe()) {
        vlog(LogDump, "ds: %04x  es: %04x ss: %04x  fs: %04x  gs: %04x", get_ds(), get_es(), get_ss(), get_fs(), get_gs());
        vlog(LogDump, "cs: %04x eip: %08x", get_cs(), get_eip());
    } else {
        dump_selector("ds: ", SegmentRegisterIndex::DS);
        dump_selector("es: ", SegmentRegisterIndex::ES);
        dump_selector("ss: ", SegmentRegisterIndex::SS);
        dump_selector("fs: ", SegmentRegisterIndex::FS);
        dump_selector("gs: ", SegmentRegisterIndex::GS);
        dump_selector("cs: ", SegmentRegisterIndex::CS);
        vlog(LogDump, "eip: %08x", get_eip());
    }
    vlog(LogDump, "cpl: %u  iopl: %u  a20: %u", get_cpl(), get_iopl(), is_a20_enabled());
    vlog(LogDump, "a%u[%u] o%u[%u] s%u x%u",
        m_effective_address_size32 ? 32 : 16,
        m_address_size32 ? 32 : 16,
        m_effective_operand_size32 ? 32 : 16,
        m_operand_size32 ? 32 : 16,
        s16() ? 16 : 32,
        x16() ? 16 : 32);

    vlog(LogDump, "cr0: %08x  cr3: %08x", get_cr0(), get_cr3());
    vlog(LogDump, "idtr: {base=%08x, limit=%04x}", m_idtr.base().get(), m_idtr.limit());
    vlog(LogDump, "gdtr: {base=%08x, limit=%04x}", m_gdtr.base().get(), m_gdtr.limit());
    vlog(LogDump, "ldtr: {base=%08x, limit=%04x, (selector=%04x)}", m_ldtr.base().get(), m_ldtr.limit(), m_ldtr.selector());
    vlog(LogDump, "  tr: {base=%08x, limit=%04x, (selector=%04x, %u-bit)}", m_tr.base.get(), m_tr.limit, m_tr.selector, m_tr.is_32bit ? 32 : 16);

    vlog(LogDump, "cf=%u pf=%u af=%u zf=%u sf=%u if=%u df=%u of=%u tf=%u nt=%u vm=%u", get_cf(), get_pf(), get_af(), get_zf(), get_sf(), get_if(), get_df(), get_of(), get_tf(), get_nt(), get_vm());

    dump_disassembled(cached_descriptor(SegmentRegisterIndex::CS), current_base_instruction_pointer());
}

static inline u8 n(u8 b)
{
    if (b < 0x20 || ((b > 127) && (b < 160)))
        return '.';
    return b;
}

void CPU::dump_flat_memory(u32 address)
{
    address &= 0xFFFFFFF0;

    u8* p = &m_memory[address];
    int rows = 16;

    for (int i = 0; i < rows; ++i) {
        vlog(LogDump,
            "%08X   %02X %02X %02X %02X %02X %02X %02X %02X - %02X %02X %02X %02X %02X %02X %02X %02X   %c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c",
            (address + i * 16),
            p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
            p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15],
            n(p[0]), n(p[1]), n(p[2]), n(p[3]), n(p[4]), n(p[5]), n(p[6]), n(p[7]),
            n(p[8]), n(p[9]), n(p[10]), n(p[11]), n(p[12]), n(p[13]), n(p[14]), n(p[15]));
        p += 16;
    }

    p = &m_memory[address];
    for (int i = 0; i < rows; ++i) {
        fprintf(stderr,
            "db 0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X\n",
            p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
            p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
        p += 16;
    }
}

void CPU::dump_raw_memory(u8* p)
{
    int rows = 16;
    vlog(LogDump, "Raw dump %p", p);
    for (int i = 0; i < rows; ++i) {
        fprintf(stderr,
            "db 0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X\n",
            p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
            p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
        p += 16;
    }
}

void CPU::dump_memory(SegmentDescriptor& descriptor, u32 offset, int rows)
{
    offset &= 0xFFFFFFF0;
    const u8* p;

    try {
        p = memory_pointer(descriptor, offset);
    } catch (...) {
        p = nullptr;
    }

    if (!p) {
        vlog(LogCPU, "dumpMemory can't dump %04x:%08x", descriptor.index(), offset);
        return;
    }

    for (int i = 0; i < rows; ++i) {
        vlog(LogDump,
            "%04x:%04x   %02X %02X %02X %02X %02X %02X %02X %02X - %02X %02X %02X %02X %02X %02X %02X %02X   %c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c",
            descriptor.index(), (offset + i * 16),
            p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
            p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15],
            n(p[0]), n(p[1]), n(p[2]), n(p[3]), n(p[4]), n(p[5]), n(p[6]), n(p[7]),
            n(p[8]), n(p[9]), n(p[10]), n(p[11]), n(p[12]), n(p[13]), n(p[14]), n(p[15]));
        p += 16;
    }

    p = memory_pointer(descriptor, offset);
    for (int i = 0; i < rows; ++i) {
        fprintf(stderr,
            "db 0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X,0x%02X\n",
            p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
            p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
        p += 16;
    }
}

void CPU::dump_memory(LogicalAddress address, int rows)
{
    auto descriptor = get_segment_descriptor(address.selector());
    return dump_memory(descriptor, address.offset(), rows);
}

static inline u16 isrSegment(CPU& cpu, u8 isr)
{
    return cpu.get_real_mode_interrupt_vector(isr).selector();
}

static inline u16 isrOffset(CPU& cpu, u8 isr)
{
    return cpu.get_real_mode_interrupt_vector(isr).offset();
}

void CPU::dump_ivt()
{
    // XXX: For alignment reasons, we're skipping INT FF
    for (int i = 0; i < 0xFF; i += 4) {
        vlog(LogDump,
            "%02x>  %04x:%04x\t%02x>  %04x:%04x\t%02x>  %04x:%04x\t%02X>  %04x:%04x",
            i + 0, isrSegment(*this, i + 0), isrOffset(*this, i + 0),
            i + 1, isrSegment(*this, i + 1), isrOffset(*this, i + 1),
            i + 2, isrSegment(*this, i + 2), isrOffset(*this, i + 2),
            i + 3, isrSegment(*this, i + 3), isrOffset(*this, i + 3));
    }
}

void CPU::dump_descriptor(const Descriptor& descriptor, const char* prefix)
{
    if (descriptor.is_null())
        vlog(LogCPU, "%s%04x (null descriptor)", prefix, descriptor.index());
    else if (descriptor.is_segment_descriptor())
        dump_descriptor(descriptor.as_segment_descriptor(), prefix);
    else
        dump_descriptor(descriptor.as_system_descriptor(), prefix);
}

void CPU::dump_descriptor(const SegmentDescriptor& descriptor, const char* prefix)
{
    if (descriptor.is_null())
        vlog(LogCPU, "%s%04x (null descriptor)", prefix, descriptor.index());
    else if (descriptor.is_code())
        dump_descriptor(descriptor.as_code_segment_descriptor(), prefix);
    else
        dump_descriptor(descriptor.as_data_segment_descriptor(), prefix);
}

void CPU::dump_descriptor(const Gate& gate, const char* prefix)
{
    vlog(LogCPU, "%s%04x (gate) { type: %s (%02x), entry:%04x:%06x, params:%u, bits:%u, p:%u, dpl:%u }",
        prefix,
        gate.index(),
        gate.type_name(),
        (u8)gate.type(),
        gate.selector(),
        gate.offset(),
        gate.parameter_count(),
        gate.d() ? 32 : 16,
        gate.present(),
        gate.dpl());
    if (gate.is_call_gate()) {
        vlog(LogCPU, "Call gate points to:");
        dump_descriptor(get_descriptor(gate.selector()), prefix);
    }
}

void CPU::dump_descriptor(const SystemDescriptor& descriptor, const char* prefix)
{
    if (descriptor.is_gate()) {
        dump_descriptor(descriptor.as_gate(), prefix);
        return;
    }
    if (descriptor.is_ldt()) {
        vlog(LogCPU, "%s%04x (system segment) { type: LDT (%02x), base:%08x e-limit:%08x, p:%u }",
            prefix,
            descriptor.index(),
            (u8)descriptor.type(),
            descriptor.as_ldt_descriptor().base(),
            descriptor.as_ldt_descriptor().effective_limit(),
            descriptor.present());
        return;
    }
    vlog(LogCPU, "%s%04x (system segment) { type: %s (%02x), bits:%u, p:%u, dpl:%u }",
        prefix,
        descriptor.index(),
        descriptor.type_name(),
        (u8)descriptor.type(),
        descriptor.d() ? 32 : 16,
        descriptor.present(),
        descriptor.dpl());
}

void CPU::dump_descriptor(const CodeSegmentDescriptor& segment, const char* prefix)
{
    vlog(LogCPU, "%s%04x (%s) { type: code, base:%08x, e-limit:%08x, bits:%u, p:%u, g:%s, dpl:%u, a:%u, r:%u, c:%u }",
        prefix,
        segment.index(),
        segment.is_global() ? "global" : " local",
        segment.base(),
        segment.effective_limit(),
        segment.d() ? 32 : 16,
        segment.present(),
        segment.granularity() ? "4k" : "1b",
        segment.dpl(),
        segment.accessed(),
        segment.conforming(),
        segment.readable());
}

void CPU::dump_descriptor(const DataSegmentDescriptor& segment, const char* prefix)
{
    vlog(LogCPU, "%s%04x (%s) { type: data, base:%08x, e-limit:%08x, bits:%u, p:%u, g:%s, dpl:%u, a:%u, w:%u, ed:%u }",
        prefix,
        segment.index(),
        segment.is_global() ? "global" : " local",
        segment.base(),
        segment.effective_limit(),
        segment.d() ? 32 : 16,
        segment.present(),
        segment.granularity() ? "4k" : "1b",
        segment.dpl(),
        segment.accessed(),
        segment.writable(),
        segment.expand_down());
}

void CPU::dump_segment(u16 index)
{
    dump_descriptor(get_descriptor(index));
}

void CPU::dump_stack(ValueSize valueSize, unsigned count)
{
    u32 sp = current_stack_pointer();
    for (unsigned i = 0; i < count; ++i) {
        if (valueSize == DWordSize) {
            u32 value = read_memory32(SegmentRegisterIndex::SS, sp);
            vlog(LogDump, "%04x:%08x (+%04x) %08x", get_ss(), sp, sp - current_stack_pointer(), value);
            sp += 4;
        }
    }
}
