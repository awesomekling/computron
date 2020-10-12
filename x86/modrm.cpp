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
#include "debug.h"
#include "debugger.h"

#define DEFAULT_TO_SS                         \
    if (!m_cpu->hasSegmentPrefix()) {         \
        m_segment = SegmentRegisterIndex::SS; \
    }

void MemoryOrRegisterReference::write_special(u32 data, bool o32)
{
    if (o32 && is_register()) {
        m_cpu->write_register<u32>(m_register_index, data);
        return;
    }
    return write<u16>(data & 0xffff);
}

FLATTEN void MemoryOrRegisterReference::resolve(CPU& cpu)
{
    m_cpu = &cpu;
    ASSERT(m_cpu->a32() == m_a32);
    if (m_a32)
        return resolve32();
    return resolve16();
}

FLATTEN void MemoryOrRegisterReference::decode(InstructionStream& stream, bool a32)
{
    m_a32 = a32;
    m_rm = stream.read_instruction8();

    if (m_a32) {
        decode32(stream);
        switch (m_displacement_bytes) {
        case 0:
            break;
        case 1:
            m_displacement32 = signExtendedTo<u32>(stream.read_instruction8());
            break;
        case 4:
            m_displacement32 = stream.read_instruction32();
            break;
        default:
            ASSERT_NOT_REACHED();
            break;
        }
    } else {
        decode16(stream);
        switch (m_displacement_bytes) {
        case 0:
            break;
        case 1:
            m_displacement16 = signExtendedTo<u16>(stream.read_instruction8());
            break;
        case 2:
            m_displacement16 = stream.read_instruction16();
            break;
        default:
            ASSERT_NOT_REACHED();
            break;
        }
    }
}

ALWAYS_INLINE void MemoryOrRegisterReference::decode16(InstructionStream&)
{
    ASSERT(!m_a32);

    switch (m_rm & 0xc0) {
    case 0:
        if ((m_rm & 0x07) == 6)
            m_displacement_bytes = 2;
        else
            ASSERT(m_displacement_bytes == 0);
        break;
    case 0x40:
        m_displacement_bytes = 1;
        break;
    case 0x80:
        m_displacement_bytes = 2;
        break;
    case 0xc0:
        m_register_index = m_rm & 7;
        break;
    }
}

ALWAYS_INLINE void MemoryOrRegisterReference::decode32(InstructionStream& stream)
{
    ASSERT(m_a32);

    switch (m_rm & 0xc0) {
    case 0:
        if ((m_rm & 0x07) == 5)
            m_displacement_bytes = 4;
        break;
    case 0x40:
        m_displacement_bytes = 1;
        break;
    case 0x80:
        m_displacement_bytes = 4;
        break;
    case 0xc0:
        m_register_index = m_rm & 7;
        return;
    }

    m_has_sib = (m_rm & 0x07) == 4;
    if (m_has_sib) {
        m_sib = stream.read_instruction8();
        if ((m_sib & 0x07) == 5) {
            switch ((m_rm >> 6) & 0x03) {
            case 0:
                ASSERT(!m_displacement_bytes || m_displacement_bytes == 4);
                m_displacement_bytes = 4;
                break;
            case 1:
                ASSERT(!m_displacement_bytes || m_displacement_bytes == 1);
                m_displacement_bytes = 1;
                break;
            case 2:
                ASSERT(!m_displacement_bytes || m_displacement_bytes == 4);
                m_displacement_bytes = 4;
                break;
            default:
                ASSERT_NOT_REACHED();
                break;
            }
        }
    }
}

ALWAYS_INLINE void MemoryOrRegisterReference::resolve16()
{
    ASSERT(m_cpu);
    ASSERT(!m_a32);
    ASSERT(m_cpu->a16());

    m_segment = m_cpu->current_segment();

    switch (m_rm & 7) {
    case 0:
        m_offset16 = m_cpu->get_bx() + m_cpu->get_si() + m_displacement16;
        break;
    case 1:
        m_offset16 = m_cpu->get_bx() + m_cpu->get_di() + m_displacement16;
        break;
    case 2:
        DEFAULT_TO_SS;
        m_offset16 = m_cpu->get_bp() + m_cpu->get_si() + m_displacement16;
        break;
    case 3:
        DEFAULT_TO_SS;
        m_offset16 = m_cpu->get_bp() + m_cpu->get_di() + m_displacement16;
        break;
    case 4:
        m_offset16 = m_cpu->get_si() + m_displacement16;
        break;
    case 5:
        m_offset16 = m_cpu->get_di() + m_displacement16;
        break;
    case 6:
        if ((m_rm & 0xc0) == 0)
            m_offset16 = m_displacement16;
        else {
            DEFAULT_TO_SS;
            m_offset16 = m_cpu->get_bp() + m_displacement16;
        }
        break;
    default:
        m_offset16 = m_cpu->get_bx() + m_displacement16;
        break;
    }
}

ALWAYS_INLINE void MemoryOrRegisterReference::resolve32()
{
    ASSERT(m_cpu);
    ASSERT(m_a32);
    ASSERT(m_cpu->a32());

    m_segment = m_cpu->current_segment();

    switch (m_rm & 0x07) {
    case 0:
        m_offset32 = m_cpu->get_eax() + m_displacement32;
        break;
    case 1:
        m_offset32 = m_cpu->get_ecx() + m_displacement32;
        break;
    case 2:
        m_offset32 = m_cpu->get_edx() + m_displacement32;
        break;
    case 3:
        m_offset32 = m_cpu->get_ebx() + m_displacement32;
        break;
    case 4:
        m_offset32 = evaluateSIB();
        break;
    case 6:
        m_offset32 = m_cpu->get_esi() + m_displacement32;
        break;
    case 7:
        m_offset32 = m_cpu->get_edi() + m_displacement32;
        break;
    default: // 5
        if ((m_rm & 0xc0) == 0x00) {
            m_offset32 = m_displacement32;
            break;
        } else {
            DEFAULT_TO_SS;
            m_offset32 = m_cpu->get_ebp() + m_displacement32;
            break;
        }
        break;
    }
}

ALWAYS_INLINE u32 MemoryOrRegisterReference::evaluateSIB()
{
    u32 scale;
    switch (m_sib & 0xC0) {
    case 0x00:
        scale = 1;
        break;
    case 0x40:
        scale = 2;
        break;
    case 0x80:
        scale = 4;
        break;
    case 0xC0:
        scale = 8;
        break;
    }
    u32 index;
    switch ((m_sib >> 3) & 0x07) {
    case 0:
        index = m_cpu->get_eax();
        break;
    case 1:
        index = m_cpu->get_ecx();
        break;
    case 2:
        index = m_cpu->get_edx();
        break;
    case 3:
        index = m_cpu->get_ebx();
        break;
    case 4:
        index = 0;
        break;
    case 5:
        index = m_cpu->get_ebp();
        break;
    case 6:
        index = m_cpu->get_esi();
        break;
    case 7:
        index = m_cpu->get_edi();
        break;
    }

    u32 base = m_displacement32;
    switch (m_sib & 0x07) {
    case 0:
        base += m_cpu->get_eax();
        break;
    case 1:
        base += m_cpu->get_ecx();
        break;
    case 2:
        base += m_cpu->get_edx();
        break;
    case 3:
        base += m_cpu->get_ebx();
        break;
    case 4:
        DEFAULT_TO_SS;
        base += m_cpu->get_esp();
        break;
    case 6:
        base += m_cpu->get_esi();
        break;
    case 7:
        base += m_cpu->get_edi();
        break;
    default: // 5
        switch ((m_rm >> 6) & 3) {
        case 0:
            break;
        case 1:
        case 2:
            DEFAULT_TO_SS;
            base += m_cpu->get_ebp();
            break;
        default:
            ASSERT_NOT_REACHED();
            break;
        }
        break;
    }

    return (scale * index) + base;
}
