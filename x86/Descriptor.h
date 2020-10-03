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
    bool isGlobal() const { return m_isGlobal; }
    u8 RPL() const { return m_RPL; }

    bool isSegmentDescriptor() const { return m_DT; }
    bool isSystemDescriptor() const { return !m_DT; }

    bool isNull() const { return m_error == NullSelector; }
    bool isOutsideTableLimits() const { return m_error == LimitExceeded; }

    unsigned DPL() const { return m_DPL; }
    bool present() const { return m_P; }
    bool D() const { return m_D; }
    bool available() const { return m_AVL; }

    unsigned type() const { return m_type; }

    bool isCode() const;
    bool isData() const;
    bool isGate() const;
    bool isTSS() const;
    bool isLDT() const;

    bool isConformingCode() const;
    bool isNonconformingCode() const;
    bool isCallGate() const;
    bool isTaskGate() const;
    bool isTrapGate() const;
    bool isInterruptGate() const;

    SegmentDescriptor& asSegmentDescriptor();
    SystemDescriptor& asSystemDescriptor();
    Gate& asGate();
    TSSDescriptor& asTSSDescriptor();
    LDTDescriptor& asLDTDescriptor();
    CodeSegmentDescriptor& asCodeSegmentDescriptor();
    DataSegmentDescriptor& asDataSegmentDescriptor();

    const SegmentDescriptor& asSegmentDescriptor() const;
    const SystemDescriptor& asSystemDescriptor() const;
    const Gate& asGate() const;
    const TSSDescriptor& asTSSDescriptor() const;
    const LDTDescriptor& asLDTDescriptor() const;
    const CodeSegmentDescriptor& asCodeSegmentDescriptor() const;
    const DataSegmentDescriptor& asDataSegmentDescriptor() const;

protected:
    u32 m_high { 0 };
    u32 m_low { 0 };
    union {
        struct {
            u32 m_segmentBase { 0 };
            u32 m_segmentLimit { 0 };
        };
        struct {
            u16 m_gateParameterCount;
            u16 m_gateSelector;
            u32 m_gateOffset;
        };
    };
    unsigned m_DPL { 0 };
    unsigned m_type { 0 };
    bool m_G { false };
    bool m_D { false };
    bool m_P { false };
    bool m_AVL { false };
    bool m_DT { false };

    u32 m_effectiveLimit { 0 };

    // These are not part of the descriptor, but metadata about the lookup that found this descriptor.
    unsigned m_index { 0xFFFFFFFF };
    bool m_isGlobal { false };
    u8 m_RPL { 0 };
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
    const char* typeName() const;

    bool isCallGate() const { return type() == CallGate_16bit || type() == CallGate_32bit; }
    bool isInterruptGate() const { return type() == InterruptGate_16bit || type() == InterruptGate_32bit; }
    bool isTrapGate() const { return type() == TrapGate_16bit || type() == TrapGate_32bit; }
    bool isTaskGate() const { return type() == TaskGate; }
    bool isGate() const { return isCallGate() || isInterruptGate() || isTrapGate() || isTaskGate(); }
    bool isTSS() const { return type() == AvailableTSS_16bit || type() == BusyTSS_16bit || type() == AvailableTSS_32bit || type() == BusyTSS_32bit; }
    bool isLDT() const { return type() == LDT; }
};

class Gate : public SystemDescriptor {
public:
    u16 selector() const { return m_gateSelector; }
    u32 offset() const { return m_gateOffset; }
    u16 parameterCount() const { return m_gateParameterCount; }

    LogicalAddress entry() const { return LogicalAddress(selector(), offset()); }

    bool is32Bit() const { return type() == InterruptGate_32bit || type() == CallGate_32bit || type() == TrapGate_32bit; }
    ValueSize size() const { return is32Bit() ? DWordSize : WordSize; }
};

// Note: TSSDescriptor uses segment base+limit despite being a system descriptor.
class TSSDescriptor : public SystemDescriptor {
public:
    LinearAddress base() const { return LinearAddress(m_segmentBase); }
    u32 limit() const { return m_segmentLimit; }
    u32 effectiveLimit() const { return m_effectiveLimit; }

    void setBusy();
    void setAvailable();

    bool isAvailable() const { return type() == AvailableTSS_16bit || type() == AvailableTSS_32bit; }
    bool isBusy() const { return type() == BusyTSS_16bit || type() == BusyTSS_32bit; }

    bool is32Bit() const { return type() == AvailableTSS_32bit || type() == BusyTSS_32bit; }
};

// Note: LDTDescriptor uses segment base+limit despite being a system descriptor.
class LDTDescriptor : public SystemDescriptor {
public:
    LinearAddress base() const { return LinearAddress(m_segmentBase); }
    u32 limit() const { return m_segmentLimit; }
    u32 effectiveLimit() const { return m_effectiveLimit; }
};

inline Gate& Descriptor::asGate()
{
    ASSERT(isGate());
    return static_cast<Gate&>(*this);
}

inline const Gate& Descriptor::asGate() const
{
    ASSERT(isGate());
    return static_cast<const Gate&>(*this);
}

inline TSSDescriptor& Descriptor::asTSSDescriptor()
{
    ASSERT(isTSS());
    return static_cast<TSSDescriptor&>(*this);
}

inline const TSSDescriptor& Descriptor::asTSSDescriptor() const
{
    ASSERT(isTSS());
    return static_cast<const TSSDescriptor&>(*this);
}

inline LDTDescriptor& Descriptor::asLDTDescriptor()
{
    ASSERT(isLDT());
    return static_cast<LDTDescriptor&>(*this);
}

inline const LDTDescriptor& Descriptor::asLDTDescriptor() const
{
    ASSERT(isLDT());
    return static_cast<const LDTDescriptor&>(*this);
}

class SegmentDescriptor : public Descriptor {
public:
    LinearAddress base() const { return LinearAddress(m_segmentBase); }
    u32 limit() const { return m_segmentLimit; }

    bool isCode() const { return (m_type & 0x8) != 0; }
    bool isData() const { return (m_type & 0x8) == 0; }
    bool accessed() const { return m_type & 0x1; }
    bool readable() const;
    bool writable() const;

    u32 effectiveLimit() const { return m_effectiveLimit; }
    bool granularity() const { return m_G; }

    LinearAddress linearAddress(u32 offset) const { return LinearAddress(m_segmentBase + offset); }
};

class CodeSegmentDescriptor : public SegmentDescriptor {
public:
    bool readable() const { return m_type & 0x2; }
    bool conforming() const { return m_type & 0x4; }
    bool is32Bit() const { return m_D; }
};

class DataSegmentDescriptor : public SegmentDescriptor {
public:
    bool writable() const { return m_type & 0x2; }
    bool expandDown() const { return m_type & 0x4; }
};

inline SegmentDescriptor& Descriptor::asSegmentDescriptor()
{
    ASSERT(isSegmentDescriptor() || isNull());
    return static_cast<SegmentDescriptor&>(*this);
}

inline SystemDescriptor& Descriptor::asSystemDescriptor()
{
    ASSERT(isSystemDescriptor());
    return static_cast<SystemDescriptor&>(*this);
}

inline CodeSegmentDescriptor& Descriptor::asCodeSegmentDescriptor()
{
    ASSERT(isCode());
    return static_cast<CodeSegmentDescriptor&>(*this);
}

inline DataSegmentDescriptor& Descriptor::asDataSegmentDescriptor()
{
    ASSERT(isData());
    return static_cast<DataSegmentDescriptor&>(*this);
}

inline const SegmentDescriptor& Descriptor::asSegmentDescriptor() const
{
    ASSERT(isSegmentDescriptor() || isNull());
    return static_cast<const SegmentDescriptor&>(*this);
}

inline const SystemDescriptor& Descriptor::asSystemDescriptor() const
{
    ASSERT(isSystemDescriptor());
    return static_cast<const SystemDescriptor&>(*this);
}

inline const CodeSegmentDescriptor& Descriptor::asCodeSegmentDescriptor() const
{
    ASSERT(isCode());
    return static_cast<const CodeSegmentDescriptor&>(*this);
}

inline const DataSegmentDescriptor& Descriptor::asDataSegmentDescriptor() const
{
    ASSERT(isData());
    return static_cast<const DataSegmentDescriptor&>(*this);
}

inline bool Descriptor::isGate() const
{
    return isSystemDescriptor() && asSystemDescriptor().isGate();
}

inline bool Descriptor::isTSS() const
{
    return isSystemDescriptor() && asSystemDescriptor().isTSS();
}

inline bool Descriptor::isLDT() const
{
    return isSystemDescriptor() && asSystemDescriptor().isLDT();
}

inline bool Descriptor::isCode() const
{
    return isSegmentDescriptor() && asSegmentDescriptor().isCode();
}

inline bool Descriptor::isData() const
{
    return isSegmentDescriptor() && asSegmentDescriptor().isData();
}

inline bool Descriptor::isNonconformingCode() const
{
    return isCode() && !asCodeSegmentDescriptor().conforming();
}

inline bool Descriptor::isConformingCode() const
{
    return isCode() && asCodeSegmentDescriptor().conforming();
}

inline bool Descriptor::isTrapGate() const
{
    return isSystemDescriptor() && asSystemDescriptor().isTrapGate();
}

inline bool Descriptor::isInterruptGate() const
{
    return isSystemDescriptor() && asSystemDescriptor().isInterruptGate();
}

inline bool Descriptor::isCallGate() const
{
    return isSystemDescriptor() && asSystemDescriptor().isCallGate();
}

inline bool Descriptor::isTaskGate() const
{
    return isSystemDescriptor() && asSystemDescriptor().isTaskGate();
}

inline bool SegmentDescriptor::readable() const
{
    if (isCode())
        return asCodeSegmentDescriptor().readable();
    return true;
}

inline bool SegmentDescriptor::writable() const
{
    if (isData())
        return asDataSegmentDescriptor().writable();
    return false;
}
