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

SegmentDescriptor CPU::getRealModeOrVM86Descriptor(WORD selector, SegmentRegisterIndex segmentRegister)
{
    ASSERT(!getPE() || getVM());
    SegmentDescriptor descriptor;
    descriptor.m_index = selector;
    descriptor.m_segmentBase = (DWORD)selector << 4;
    descriptor.m_segmentLimit = 0xffff;
    descriptor.m_effectiveLimit = 0xffff;
    descriptor.m_RPL = 0;
    descriptor.m_D = false;
    descriptor.m_DT = true;
    descriptor.m_P = true;
    descriptor.m_isGlobal = true;
    if (segmentRegister == SegmentRegisterIndex::CS) {
        // Code + Readable
        descriptor.m_type |= 0x8 | 0x2;
    } else {
        // Data + Writable
        descriptor.m_type |= 0x2;
    }
    return descriptor;
}

Descriptor CPU::getDescriptor(WORD selector)
{
    if ((selector & 0xfffc) == 0)
        return ErrorDescriptor(Descriptor::NullSelector);

    bool isGlobal = (selector & 0x04) == 0;
    if (isGlobal)
        return getDescriptor(m_GDTR, selector, true);
    return getDescriptor(m_LDTR, selector, true);
}

Descriptor CPU::getInterruptDescriptor(BYTE number)
{
    ASSERT(getPE());
    return getDescriptor(m_IDTR, number, false);
}

SegmentDescriptor CPU::getSegmentDescriptor(WORD selector)
{
    if (!getPE() || getVM())
        return getRealModeOrVM86Descriptor(selector);
    auto descriptor = getDescriptor(selector);
    if (descriptor.isNull())
        return SegmentDescriptor();
    return descriptor.asSegmentDescriptor();
}

Descriptor CPU::getDescriptor(DescriptorTableRegister& tableRegister, WORD index, bool indexIsSelector)
{
    if (indexIsSelector && (index & 0xfffc) == 0)
        return ErrorDescriptor(Descriptor::NullSelector);

    Descriptor descriptor;
    DWORD tableIndex;

    if (indexIsSelector) {
        descriptor.m_isGlobal = (index & 0x04) == 0;
        descriptor.m_RPL = index & 3;
        tableIndex = index & 0xfffffff8;
    } else {
        tableIndex = index * 8;
    }

    descriptor.m_index = index;
    if (tableIndex >= tableRegister.limit()) {
        vlog(LogCPU, "Selector 0x%04x >= %s.limit (0x%04x).", index, tableRegister.name(), tableRegister.limit());
        return ErrorDescriptor(Descriptor::LimitExceeded);
    }

    DWORD hi = readMemoryMetal32(tableRegister.base().offset(tableIndex + 4));
    DWORD lo = readMemoryMetal32(tableRegister.base().offset(tableIndex));

    descriptor.m_G = (hi >> 23) & 1; // Limit granularity, 0=1b, 1=4kB
    descriptor.m_D = (hi >> 22) & 1;
    descriptor.m_AVL = (hi >> 20) & 1;
    descriptor.m_P = (hi >> 15) & 1;
    descriptor.m_DPL = (hi >> 13) & 3; // Privilege (ring) level
    descriptor.m_DT = (hi >> 12) & 1;
    descriptor.m_type = (hi >> 8) & 0xF;

    if (descriptor.isGate()) {
        descriptor.m_gateSelector = lo >> 16;
        descriptor.m_gateParameterCount = hi & 0x1f;
        descriptor.m_gateOffset = (hi & 0xffff0000) | (lo & 0xffff);
        descriptor.m_D = descriptor.asGate().is32Bit();
    } else {
        descriptor.m_segmentBase = (hi & 0xFF000000) | ((hi & 0xFF) << 16) | ((lo >> 16) & 0xFFFF);
        descriptor.m_segmentLimit = (hi & 0xF0000) | (lo & 0xFFFF);
        if (descriptor.m_G)
            descriptor.m_effectiveLimit = (descriptor.m_segmentLimit << 12) | 0xfff;
        else
            descriptor.m_effectiveLimit = descriptor.m_segmentLimit;
    }

    descriptor.m_high = hi;
    descriptor.m_low = lo;

    return descriptor;
}

const char* SystemDescriptor::typeName() const
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

void TSSDescriptor::setBusy()
{
    m_type |= 2;
    m_high |= 0x200;
}

void TSSDescriptor::setAvailable()
{
    m_type &= ~2;
    m_high &= ~0x200;
}

void CPU::writeToGDT(Descriptor& descriptor)
{
    ASSERT(descriptor.isGlobal());
    writeMemoryMetal32(m_GDTR.base().offset(descriptor.index() + 4), descriptor.m_high);
    writeMemoryMetal32(m_GDTR.base().offset(descriptor.index()), descriptor.m_low);
}
