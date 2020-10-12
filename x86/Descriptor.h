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

#pragma once

#include "Common.h"
#include "debug.h"
#include "types.h"

class CodeSegmentDescriptor;
class DataSegmentDescriptor;
class Gate;
class LDTDescriptor;
class SegmentDescriptor;
class SystemDescriptor;
class TSSDescriptor;

class Descriptor {
    friend class CPU;

public:
    enum Error {
        NoError,
        LimitExceeded,
        NullSelector,
    };

    Descriptor() { }

    unsigned index() const { return m_index; }
    bool is_global() const { return m_global; }
    u8 RPL() const { return m_rpl; }

    bool is_segment_descriptor() const { return m_dt; }
    bool is_system_descriptor() const { return !m_dt; }

    bool is_null() const { return m_error == NullSelector; }
    bool is_outside_table_limits() const { return m_error == LimitExceeded; }

    unsigned dpl() const { return m_dpl; }
    bool present() const { return m_p; }
    bool d() const { return m_d; }
    bool available() const { return m_avl; }

    unsigned type() const { return m_type; }

    bool is_code() const;
    bool is_data() const;
    bool is_gate() const;
    bool is_tss() const;
    bool is_ldt() const;

    bool is_conforming_code() const;
    bool is_nonconforming_code() const;
    bool is_call_gate() const;
    bool is_task_gate() const;
    bool is_trap_gate() const;
    bool is_interrupt_gate() const;

    SegmentDescriptor& as_segment_descriptor();
    SystemDescriptor& as_system_descriptor();
    Gate& as_gate();
    TSSDescriptor& as_tss_descriptor();
    LDTDescriptor& as_ldt_descriptor();
    CodeSegmentDescriptor& as_code_segment_descriptor();
    DataSegmentDescriptor& as_data_segment_descriptor();

    const SegmentDescriptor& as_segment_descriptor() const;
    const SystemDescriptor& as_system_descriptor() const;
    const Gate& as_gate() const;
    const TSSDescriptor& as_tss_descriptor() const;
    const LDTDescriptor& as_ldt_descriptor() const;
    const CodeSegmentDescriptor& as_code_segment_descriptor() const;
    const DataSegmentDescriptor& as_data_segment_descriptor() const;

protected:
    u32 m_high { 0 };
    u32 m_low { 0 };
    union {
        struct {
            u32 m_segment_base { 0 };
            u32 m_segment_limit { 0 };
        };
        struct {
            u16 m_gate_parameter_count;
            u16 m_gate_selector;
            u32 m_gate_offset;
        };
    };
    unsigned m_dpl { 0 };
    unsigned m_type { 0 };
    bool m_g { false };
    bool m_d { false };
    bool m_p { false };
    bool m_avl { false };
    bool m_dt { false };

    u32 m_effective_limit { 0 };

    // These are not part of the descriptor, but metadata about the lookup that found this descriptor.
    unsigned m_index { 0xFFFFFFFF };
    bool m_global { false };
    u8 m_rpl { 0 };
    Error m_error { NoError };

    bool m_loaded_in_ss { false };
};

class ErrorDescriptor : public Descriptor {
public:
    explicit ErrorDescriptor(Error error) { m_error = error; }
};

class SystemDescriptor : public Descriptor {
public:
    enum Type {
        Invalid = 0,
        AvailableTSS_16bit = 0x1,
        LDT = 0x2,
        BusyTSS_16bit = 0x3,
        CallGate_16bit = 0x4,
        TaskGate = 0x5,
        InterruptGate_16bit = 0x6,
        TrapGate_16bit = 0x7,
        AvailableTSS_32bit = 0x9,
        BusyTSS_32bit = 0xb,
        CallGate_32bit = 0xc,
        InterruptGate_32bit = 0xe,
        TrapGate_32bit = 0xf,
    };

    Type type() const { return static_cast<Type>(m_type); }
    const char* type_name() const;

    bool is_call_gate() const { return type() == CallGate_16bit || type() == CallGate_32bit; }
    bool is_interrupt_gate() const { return type() == InterruptGate_16bit || type() == InterruptGate_32bit; }
    bool is_trap_gate() const { return type() == TrapGate_16bit || type() == TrapGate_32bit; }
    bool is_task_gate() const { return type() == TaskGate; }
    bool is_gate() const { return is_call_gate() || is_interrupt_gate() || is_trap_gate() || is_task_gate(); }
    bool is_tss() const { return type() == AvailableTSS_16bit || type() == BusyTSS_16bit || type() == AvailableTSS_32bit || type() == BusyTSS_32bit; }
    bool is_ldt() const { return type() == LDT; }
};

class Gate : public SystemDescriptor {
public:
    u16 selector() const { return m_gate_selector; }
    u32 offset() const { return m_gate_offset; }
    u16 parameter_count() const { return m_gate_parameter_count; }

    LogicalAddress entry() const { return LogicalAddress(selector(), offset()); }

    bool is_32bit() const { return type() == InterruptGate_32bit || type() == CallGate_32bit || type() == TrapGate_32bit; }
    ValueSize size() const { return is_32bit() ? DWordSize : WordSize; }
};

// Note: TSSDescriptor uses segment base+limit despite being a system descriptor.
class TSSDescriptor : public SystemDescriptor {
public:
    LinearAddress base() const { return LinearAddress(m_segment_base); }
    u32 limit() const { return m_segment_limit; }
    u32 effective_limit() const { return m_effective_limit; }

    void set_busy();
    void set_available();

    bool is_available() const { return type() == AvailableTSS_16bit || type() == AvailableTSS_32bit; }
    bool is_busy() const { return type() == BusyTSS_16bit || type() == BusyTSS_32bit; }

    bool is_32bit() const { return type() == AvailableTSS_32bit || type() == BusyTSS_32bit; }
};

// Note: LDTDescriptor uses segment base+limit despite being a system descriptor.
class LDTDescriptor : public SystemDescriptor {
public:
    LinearAddress base() const { return LinearAddress(m_segment_base); }
    u32 limit() const { return m_segment_limit; }
    u32 effective_limit() const { return m_effective_limit; }
};

inline Gate& Descriptor::as_gate()
{
    ASSERT(is_gate());
    return static_cast<Gate&>(*this);
}

inline const Gate& Descriptor::as_gate() const
{
    ASSERT(is_gate());
    return static_cast<const Gate&>(*this);
}

inline TSSDescriptor& Descriptor::as_tss_descriptor()
{
    ASSERT(is_tss());
    return static_cast<TSSDescriptor&>(*this);
}

inline const TSSDescriptor& Descriptor::as_tss_descriptor() const
{
    ASSERT(is_tss());
    return static_cast<const TSSDescriptor&>(*this);
}

inline LDTDescriptor& Descriptor::as_ldt_descriptor()
{
    ASSERT(is_ldt());
    return static_cast<LDTDescriptor&>(*this);
}

inline const LDTDescriptor& Descriptor::as_ldt_descriptor() const
{
    ASSERT(is_ldt());
    return static_cast<const LDTDescriptor&>(*this);
}

class SegmentDescriptor : public Descriptor {
public:
    LinearAddress base() const { return LinearAddress(m_segment_base); }
    u32 limit() const { return m_segment_limit; }

    bool is_code() const { return (m_type & 0x8) != 0; }
    bool is_data() const { return (m_type & 0x8) == 0; }
    bool accessed() const { return m_type & 0x1; }
    bool readable() const;
    bool writable() const;

    u32 effective_limit() const { return m_effective_limit; }
    bool granularity() const { return m_g; }

    LinearAddress linear_address(u32 offset) const { return LinearAddress(m_segment_base + offset); }
};

class CodeSegmentDescriptor : public SegmentDescriptor {
public:
    bool readable() const { return m_type & 0x2; }
    bool conforming() const { return m_type & 0x4; }
    bool is_32bit() const { return m_d; }
};

class DataSegmentDescriptor : public SegmentDescriptor {
public:
    bool writable() const { return m_type & 0x2; }
    bool expand_down() const { return m_type & 0x4; }
};

inline SegmentDescriptor& Descriptor::as_segment_descriptor()
{
    ASSERT(is_segment_descriptor() || is_null());
    return static_cast<SegmentDescriptor&>(*this);
}

inline SystemDescriptor& Descriptor::as_system_descriptor()
{
    ASSERT(is_system_descriptor());
    return static_cast<SystemDescriptor&>(*this);
}

inline CodeSegmentDescriptor& Descriptor::as_code_segment_descriptor()
{
    ASSERT(is_code());
    return static_cast<CodeSegmentDescriptor&>(*this);
}

inline DataSegmentDescriptor& Descriptor::as_data_segment_descriptor()
{
    ASSERT(is_data());
    return static_cast<DataSegmentDescriptor&>(*this);
}

inline const SegmentDescriptor& Descriptor::as_segment_descriptor() const
{
    ASSERT(is_segment_descriptor() || is_null());
    return static_cast<const SegmentDescriptor&>(*this);
}

inline const SystemDescriptor& Descriptor::as_system_descriptor() const
{
    ASSERT(is_system_descriptor());
    return static_cast<const SystemDescriptor&>(*this);
}

inline const CodeSegmentDescriptor& Descriptor::as_code_segment_descriptor() const
{
    ASSERT(is_code());
    return static_cast<const CodeSegmentDescriptor&>(*this);
}

inline const DataSegmentDescriptor& Descriptor::as_data_segment_descriptor() const
{
    ASSERT(is_data());
    return static_cast<const DataSegmentDescriptor&>(*this);
}

inline bool Descriptor::is_gate() const
{
    return is_system_descriptor() && as_system_descriptor().is_gate();
}

inline bool Descriptor::is_tss() const
{
    return is_system_descriptor() && as_system_descriptor().is_tss();
}

inline bool Descriptor::is_ldt() const
{
    return is_system_descriptor() && as_system_descriptor().is_ldt();
}

inline bool Descriptor::is_code() const
{
    return is_segment_descriptor() && as_segment_descriptor().is_code();
}

inline bool Descriptor::is_data() const
{
    return is_segment_descriptor() && as_segment_descriptor().is_data();
}

inline bool Descriptor::is_nonconforming_code() const
{
    return is_code() && !as_code_segment_descriptor().conforming();
}

inline bool Descriptor::is_conforming_code() const
{
    return is_code() && as_code_segment_descriptor().conforming();
}

inline bool Descriptor::is_trap_gate() const
{
    return is_system_descriptor() && as_system_descriptor().is_trap_gate();
}

inline bool Descriptor::is_interrupt_gate() const
{
    return is_system_descriptor() && as_system_descriptor().is_interrupt_gate();
}

inline bool Descriptor::is_call_gate() const
{
    return is_system_descriptor() && as_system_descriptor().is_call_gate();
}

inline bool Descriptor::is_task_gate() const
{
    return is_system_descriptor() && as_system_descriptor().is_task_gate();
}

inline bool SegmentDescriptor::readable() const
{
    if (is_code())
        return as_code_segment_descriptor().readable();
    return true;
}

inline bool SegmentDescriptor::writable() const
{
    if (is_data())
        return as_data_segment_descriptor().writable();
    return false;
}
