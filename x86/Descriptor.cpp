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

#include "Descriptor.h"
#include "CPU.h"
#include "debugger.h"

SegmentDescriptor CPU::get_real_mode_or_vm86_descriptor(u16 selector, SegmentRegisterIndex segment_register)
{
    ASSERT(!get_pe() || get_vm());
    SegmentDescriptor descriptor;
    descriptor.m_index = selector;
    descriptor.m_segment_base = (u32)selector << 4;
    descriptor.m_segment_limit = 0xffff;
    descriptor.m_effective_limit = 0xffff;
    descriptor.m_rpl = 0;
    descriptor.m_d = false;
    descriptor.m_dt = true;
    descriptor.m_p = true;
    descriptor.m_global = true;
    if (segment_register == SegmentRegisterIndex::CS) {
        // Code + Readable
        descriptor.m_type |= 0x8 | 0x2;
    } else {
        // Data + Writable
        descriptor.m_type |= 0x2;
    }
    return descriptor;
}

Descriptor CPU::get_descriptor(u16 selector)
{
    if ((selector & 0xfffc) == 0)
        return ErrorDescriptor(Descriptor::NullSelector);

    bool isGlobal = (selector & 0x04) == 0;
    if (isGlobal)
        return get_descriptor(m_gdtr, selector, true);
    return get_descriptor(m_ldtr, selector, true);
}

Descriptor CPU::get_interrupt_descriptor(u8 number)
{
    ASSERT(get_pe());
    return get_descriptor(m_idtr, number, false);
}

SegmentDescriptor CPU::get_segment_descriptor(u16 selector)
{
    if (!get_pe() || get_vm())
        return get_real_mode_or_vm86_descriptor(selector);
    auto descriptor = get_descriptor(selector);
    if (descriptor.is_null())
        return SegmentDescriptor();
    return descriptor.as_segment_descriptor();
}

Descriptor CPU::get_descriptor(DescriptorTableRegister& table_register, u16 index, bool index_is_selector)
{
    if (index_is_selector && (index & 0xfffc) == 0)
        return ErrorDescriptor(Descriptor::NullSelector);

    Descriptor descriptor;
    u32 tableIndex;

    if (index_is_selector) {
        descriptor.m_global = (index & 0x04) == 0;
        descriptor.m_rpl = index & 3;
        tableIndex = index & 0xfffffff8;
    } else {
        tableIndex = index * 8;
    }

    descriptor.m_index = index;
    if (tableIndex >= table_register.limit()) {
        vlog(LogCPU, "Selector 0x%04x >= %s.limit (0x%04x).", index, table_register.name(), table_register.limit());
        return ErrorDescriptor(Descriptor::LimitExceeded);
    }

    u32 hi = read_memory_metal32(table_register.base().offset(tableIndex + 4));
    u32 lo = read_memory_metal32(table_register.base().offset(tableIndex));

    descriptor.m_g = (hi >> 23) & 1; // Limit granularity, 0=1b, 1=4kB
    descriptor.m_d = (hi >> 22) & 1;
    descriptor.m_avl = (hi >> 20) & 1;
    descriptor.m_p = (hi >> 15) & 1;
    descriptor.m_dpl = (hi >> 13) & 3; // Privilege (ring) level
    descriptor.m_dt = (hi >> 12) & 1;
    descriptor.m_type = (hi >> 8) & 0xF;

    if (descriptor.is_gate()) {
        descriptor.m_gate_selector = lo >> 16;
        descriptor.m_gate_parameter_count = hi & 0x1f;
        descriptor.m_gate_offset = (hi & 0xffff0000) | (lo & 0xffff);
        descriptor.m_d = descriptor.as_gate().is_32bit();
    } else {
        descriptor.m_segment_base = (hi & 0xFF000000) | ((hi & 0xFF) << 16) | ((lo >> 16) & 0xFFFF);
        descriptor.m_segment_limit = (hi & 0xF0000) | (lo & 0xFFFF);
        if (descriptor.m_g)
            descriptor.m_effective_limit = (descriptor.m_segment_limit << 12) | 0xfff;
        else
            descriptor.m_effective_limit = descriptor.m_segment_limit;
    }

    descriptor.m_high = hi;
    descriptor.m_low = lo;

    return descriptor;
}

const char* SystemDescriptor::type_name() const
{
    switch (m_type) {
    case SystemDescriptor::Invalid:
        return "Invalid";
    case SystemDescriptor::AvailableTSS_16bit:
        return "AvailableTSS_16bit";
    case SystemDescriptor::LDT:
        return "LDT";
    case SystemDescriptor::BusyTSS_16bit:
        return "BusyTSS_16bit";
    case SystemDescriptor::CallGate_16bit:
        return "CallGate_16bit";
    case SystemDescriptor::TaskGate:
        return "TaskGate";
    case SystemDescriptor::InterruptGate_16bit:
        return "InterruptGate_16bit";
    case SystemDescriptor::TrapGate_16bit:
        return "TrapGate_16bit";
    case SystemDescriptor::AvailableTSS_32bit:
        return "AvailableTSS_32bit";
    case SystemDescriptor::BusyTSS_32bit:
        return "BusyTSS_32bit";
    case SystemDescriptor::CallGate_32bit:
        return "CallGate_32bit";
    case SystemDescriptor::InterruptGate_32bit:
        return "InterruptGate_32bit";
    case SystemDescriptor::TrapGate_32bit:
        return "TrapGate_32bit";
    default:
        return "(Reserved)";
    }
}

void TSSDescriptor::set_busy()
{
    m_type |= 2;
    m_high |= 0x200;
}

void TSSDescriptor::set_available()
{
    m_type &= ~2;
    m_high &= ~0x200;
}

void CPU::write_to_gdt(Descriptor& descriptor)
{
    ASSERT(descriptor.is_global());
    write_memory_metal32(m_gdtr.base().offset(descriptor.index() + 4), descriptor.m_high);
    write_memory_metal32(m_gdtr.base().offset(descriptor.index()), descriptor.m_low);
}
