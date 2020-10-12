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
#include "Descriptor.h"
#include "Instruction.h"
#include "OwnPtr.h"
#include "debug.h"
#include <QtCore/QVector>
#include <set>

class Debugger;
class Machine;
class MemoryProvider;
class CPU;
class TSS;

struct WatchedAddress {
    WatchedAddress() { }
    WatchedAddress(QString n, u32 a, ValueSize s, bool b = false)
        : name(n)
        , address(a)
        , size(s)
        , breakOnChange(b)
    {
    }
    QString name;
    PhysicalAddress address { 0xBEEFBABE };
    ValueSize size { ByteSize };
    bool breakOnChange { false };
    static const u64 neverSeen = 0xFFFFFFFFFFFFFFFF;
    u64 lastSeenValue { neverSeen };
};

enum class JumpType {
    Internal,
    IRET,
    RETF,
    INT,
    CALL,
    JMP
};

struct PageTableEntryFlags {
    enum Flags {
        Present = 0x01,
        ReadWrite = 0x02,
        UserSupervisor = 0x04,
        Accessed = 0x20,
        Dirty = 0x40,
    };
};

struct PageFaultFlags {
    enum Flags {
        NotPresent = 0x00,
        ProtectionViolation = 0x01,
        Read = 0x00,
        Write = 0x02,
        UserMode = 0x04,
        SupervisorMode = 0x00,
        InstructionFetch = 0x08,
    };
};

struct HardwareInterruptDuringREP {
};

class Exception {
public:
    Exception(u8 num, u16 code, u32 address, const QString& reason)
        : m_num(num)
        , m_code(code)
        , m_address(address)
        , m_hasCode(true)
        , m_reason(reason)
    {
    }

    Exception(u8 num, u16 code, const QString& reason)
        : m_num(num)
        , m_code(code)
        , m_hasCode(true)
        , m_reason(reason)
    {
    }

    Exception(u8 num, const QString& reason)
        : m_num(num)
        , m_hasCode(false)
        , m_reason(reason)
    {
    }

    ~Exception() { }

    u8 num() const { return m_num; }
    u16 code() const { return m_code; }
    bool hasCode() const { return m_hasCode; }
    u32 address() const { return m_address; }
    QString reason() const { return m_reason; }

private:
    u8 m_num { 0 };
    u16 m_code { 0 };
    u32 m_address { 0 };
    bool m_hasCode { false };
    QString m_reason;
};

union PartAddressableRegister {
    struct {
        u64 full_u64;
    };
#ifdef CT_BIG_ENDIAN
    struct {
        u32 __high_u32;
        u32 full_u32;
    };
    struct {
        u16 __high_u16;
        u16 low_u16;
    };
    struct {
        u16 __high_u16_2;
        u8 high_u8;
        u8 low_u8;
    };
#else
    struct {
        u32 full_u32;
        u32 __high_u32;
    };
    struct {
        u16 low_u16;
        u16 __high_u16;
    };
    struct {
        u8 low_u8;
        u8 high_u8;
        u8 __high_u16_2;
    };
#endif
};

class DescriptorTableRegister {
public:
    explicit DescriptorTableRegister(const char* name)
        : m_name(name)
    {
    }

    const char* name() const { return m_name; }
    LinearAddress base() const { return m_base; }
    u16 limit() const { return m_limit; }
    u16 selector() const { return m_selector; }

    void set_base(LinearAddress address) { m_base = address; }
    void set_limit(u16 limit) { m_limit = limit; }
    void set_selector(u16 selector) { m_selector = selector; }

    void clear()
    {
        m_base = LinearAddress();
        m_limit = 0xffff;
        m_selector = 0;
    }

private:
    const char* m_name { nullptr };
    LinearAddress m_base { 0 };
    u16 m_limit { 0xffff };
    u16 m_selector { 0 };
};

class CPU final : public InstructionStream {
    friend void build_opcode_tables_if_needed();
    friend class Debugger;

public:
    explicit CPU(Machine&);
    ~CPU();

    struct Flag {
        enum Flags : u32 {
            CF = 0x0001,
            PF = 0x0004,
            AF = 0x0010,
            ZF = 0x0040,
            SF = 0x0080,
            TF = 0x0100,
            IF = 0x0200,
            DF = 0x0400,
            OF = 0x0800,
            IOPL = 0x3000, // Note: this is a 2-bit field
            NT = 0x4000,
            RF = 0x10000,
            VM = 0x20000,
            AC = 0x40000,
            VIF = 0x80000,
            VIP = 0x100000,
            ID = 0x200000,
        };
    };

    struct CR0 {
        enum Bits : u32 {
            PE = 1u << 0,
            EM = 1u << 2,
            TS = 1u << 3,
            WP = 1u << 16,
            PG = 1u << 31,
        };
    };

    struct CR4 {
        enum Bits : u32 {
            VME = 1u << 0,
            PVI = 1u << 1,
            TSD = 1u << 2,
        };
    };

    void register_memory_provider(MemoryProvider&);
    MemoryProvider* memory_provider_for_address(PhysicalAddress);

    void recompute_main_loop_needs_slow_stuff();

    u64 cycle() const { return m_cycle; }

    void reset();

    Machine& machine() const { return m_machine; }

    std::set<LogicalAddress>& breakpoints() { return m_breakpoints; }

    enum class MemoryAccessType { Read,
        Write,
        Execute,
        InternalPointer };

    enum RegisterIndex8 {
        RegisterAL = 0,
        RegisterCL,
        RegisterDL,
        RegisterBL,
        RegisterAH,
        RegisterCH,
        RegisterDH,
        RegisterBH
    };

    enum RegisterIndex16 {
        RegisterAX = 0,
        RegisterCX,
        RegisterDX,
        RegisterBX,
        RegisterSP,
        RegisterBP,
        RegisterSI,
        RegisterDI
    };

    enum RegisterIndex32 {
        RegisterEAX = 0,
        RegisterECX,
        RegisterEDX,
        RegisterEBX,
        RegisterESP,
        RegisterEBP,
        RegisterESI,
        RegisterEDI
    };

    class TransactionalPopper {
    public:
        TransactionalPopper(CPU& cpu)
            : m_cpu(cpu)
        {
        }
        ~TransactionalPopper() { }

        // FIXME: Check SS limits as we go.
        void commit() { m_cpu.adjust_stack_pointer(m_offset); }

        u32 pop32()
        {
            u32 new_esp = m_cpu.current_stack_pointer() + m_offset;
            if (m_cpu.s16())
                new_esp &= 0xffff;
            auto data = m_cpu.read_memory32(SegmentRegisterIndex::SS, new_esp);
            m_offset += 4;
            return data;
        }
        u16 pop16()
        {
            u32 new_esp = m_cpu.current_stack_pointer() + m_offset;
            if (m_cpu.s16())
                new_esp &= 0xffff;
            auto data = m_cpu.read_memory16(SegmentRegisterIndex::SS, new_esp);
            m_offset += 2;
            return data;
        }
        u32 pop_operand_sized_value() { return m_cpu.o16() ? pop16() : pop32(); }
        void adjust_stack_pointer(int adjustment) { m_offset += adjustment; }
        u32 adjusted_stack_pointer() const { return m_cpu.current_stack_pointer() + m_offset; }

    private:
        CPU& m_cpu;
        int m_offset { 0 };
    };

    void dump_segment(u16 index);
    void dump_descriptor(const Descriptor&, const char* prefix = "");
    void dump_descriptor(const Gate&, const char* prefix = "");
    void dump_descriptor(const SegmentDescriptor&, const char* prefix = "");
    void dump_descriptor(const SystemDescriptor&, const char* prefix = "");
    void dump_descriptor(const CodeSegmentDescriptor&, const char* prefix = "");
    void dump_descriptor(const DataSegmentDescriptor&, const char* prefix = "");

    LogicalAddress get_real_mode_interrupt_vector(u8 index);
    SegmentDescriptor get_real_mode_or_vm86_descriptor(u16 selector, SegmentRegisterIndex = SegmentRegisterIndex::None);
    Descriptor get_descriptor(u16 selector);
    SegmentDescriptor get_segment_descriptor(u16 selector);
    Descriptor get_interrupt_descriptor(u8 number);
    Descriptor get_descriptor(DescriptorTableRegister&, u16 index, bool index_is_selector);

    SegmentRegisterIndex current_segment() const { return m_segment_prefix == SegmentRegisterIndex::None ? SegmentRegisterIndex::DS : m_segment_prefix; }
    bool hasSegmentPrefix() const { return m_segment_prefix != SegmentRegisterIndex::None; }

    void set_segment_prefix(SegmentRegisterIndex segment)
    {
        m_segment_prefix = segment;
    }

    void clearPrefix()
    {
        m_segment_prefix = SegmentRegisterIndex::None;
        m_effective_address_size32 = m_address_size32;
        m_effective_operand_size32 = m_operand_size32;
    }

    // Extended memory size in KiB (will be reported by CMOS)
    u32 extended_memory_size() const { return m_extended_memory_size; }
    void set_extended_memory_size(u32 size) { m_extended_memory_size = size; }

    // Conventional memory size in KiB (will be reported by CMOS)
    u32 base_memory_size() const { return m_base_memory_size; }
    void set_base_memory_size(u32 size) { m_base_memory_size = size; }

    void set_memory_size_and_reallocate_if_needed(u32);

    void kill();

    void set_a20_enabled(bool value) { m_a20_enabled = value; }
    bool is_a20_enabled() const { return m_a20_enabled; }

    u32 a20_mask() const { return is_a20_enabled() ? 0xFFFFFFFF : 0xFFEFFFFF; }

    enum class InterruptSource {
        Internal = 0,
        External = 1
    };

    void real_mode_interrupt(u8 isr, InterruptSource);
    void protected_mode_interrupt(u8 isr, InterruptSource, QVariant error_code);
    void interrupt(u8 isr, InterruptSource, QVariant errorCode = QVariant());
    void interrupt_to_task_gate(u8 isr, InterruptSource, QVariant errorCode, Gate&);

    void interrupt_from_vm86_mode(Gate&, u32 offset, CodeSegmentDescriptor&, InterruptSource, QVariant errorCode);
    void iret_to_vm86_mode(TransactionalPopper&, LogicalAddress, u32 flags);
    void iret_from_vm86_mode();
    void iret_from_real_mode();

    Exception GeneralProtectionFault(u16 selector, const QString& reason);
    Exception StackFault(u16 selector, const QString& reason);
    Exception NotPresent(u16 selector, const QString& reason);
    Exception InvalidTSS(u16 selector, const QString& reason);
    Exception PageFault(LinearAddress, PageFaultFlags::Flags, MemoryAccessType, bool inUserMode, const char* faultTable, u32 pde, u32 pte = 0);
    Exception DivideError(const QString& reason);
    Exception InvalidOpcode(const QString& reason = QString());
    Exception BoundRangeExceeded(const QString& reason);

    void raise_exception(const Exception&);

    void set_if(bool value) { this->m_if = value; }
    void set_cf(bool value) { this->m_cf = value; }
    void set_df(bool value) { this->m_df = value; }
    void set_sf(bool value)
    {
        m_dirty_flags &= ~Flag::SF;
        this->m_sf = value;
    }
    void set_af(bool value) { this->m_af = value; }
    void set_tf(bool value) { this->m_tf = value; }
    void set_of(bool value) { this->m_of = value; }
    void set_pf(bool value)
    {
        m_dirty_flags &= ~Flag::PF;
        this->m_pf = value;
    }
    void set_zf(bool value)
    {
        m_dirty_flags &= ~Flag::ZF;
        this->m_zf = value;
    }
    void set_vif(bool value) { this->m_vif = value; }
    void set_nt(bool value) { this->m_nt = value; }
    void set_rf(bool value) { this->m_rf = value; }
    void set_vm(bool value) { this->m_vm = value; }
    void set_iopl(unsigned int value) { this->m_iopl = value; }

    bool get_if() const { return this->m_if; }
    bool get_cf() const { return this->m_cf; }
    bool get_df() const { return this->m_df; }
    bool get_sf() const;
    bool get_af() const { return this->m_af; }
    bool get_tf() const { return this->m_tf; }
    bool get_of() const { return this->m_of; }
    bool get_pf() const;
    bool get_zf() const;

    unsigned int get_iopl() const { return this->m_iopl; }

    u8 get_cpl() const { return cached_descriptor(SegmentRegisterIndex::CS).RPL(); }
    void set_cpl(u8);

    bool get_nt() const { return this->m_nt; }
    bool get_vip() const { return this->m_vip; }
    bool get_vif() const { return this->m_vif; }
    bool get_vm() const { return this->m_vm; }
    bool get_pe() const { return m_cr0 & CR0::PE; }
    bool get_pg() const { return m_cr0 & CR0::PG; }
    bool get_vme() const { return m_cr4 & CR4::VME; }
    bool get_pvi() const { return m_cr4 & CR4::PVI; }
    bool get_tsd() const { return m_cr4 & CR4::TSD; }

    u16 get_cs() const { return this->m_cs; }
    u16 get_ip() const { return m_eip & 0xffff; }
    u32 get_eip() const { return m_eip; }

    u16 get_ds() const { return this->m_ds; }
    u16 get_es() const { return this->m_es; }
    u16 get_ss() const { return this->m_ss; }
    u16 get_fs() const { return this->m_fs; }
    u16 get_gs() const { return this->m_gs; }

    void set_cs(u16 cs);
    void set_ds(u16 ds);
    void set_es(u16 es);
    void set_ss(u16 ss);
    void set_fs(u16 fs);
    void set_gs(u16 gs);

    void set_ip(u16 ip) { set_eip(ip); }
    void set_eip(u32 eip) { m_eip = eip; }

    u16 read_segment_register(SegmentRegisterIndex segreg) const { return *m_segment_map[static_cast<int>(segreg)]; }

    u32 get_control_register(int register_index) const { return *m_control_register_map[register_index]; }
    void set_control_register(int register_index, u32 value) { *m_control_register_map[register_index] = value; }

    u32 get_debug_register(int register_index) const { return *m_debug_register_map[register_index]; }
    void set_debug_register(int register_index, u32 value) { *m_debug_register_map[register_index] = value; }

    u8& mutable_reg8(RegisterIndex8 index) { return *m_byte_registers[index]; }
    u16& mutable_reg16(RegisterIndex16 index) { return m_gpr[index].low_u16; }
    u32& mutable_reg32(RegisterIndex32 index) { return m_gpr[index].full_u32; }

    u32 get_eax() const { return read_register<u32>(RegisterEAX); }
    u32 get_ebx() const { return read_register<u32>(RegisterEBX); }
    u32 get_ecx() const { return read_register<u32>(RegisterECX); }
    u32 get_edx() const { return read_register<u32>(RegisterEDX); }
    u32 get_esi() const { return read_register<u32>(RegisterESI); }
    u32 get_edi() const { return read_register<u32>(RegisterEDI); }
    u32 get_esp() const { return read_register<u32>(RegisterESP); }
    u32 get_ebp() const { return read_register<u32>(RegisterEBP); }

    u16 get_ax() const { return read_register<u16>(RegisterAX); }
    u16 get_bx() const { return read_register<u16>(RegisterBX); }
    u16 get_cx() const { return read_register<u16>(RegisterCX); }
    u16 get_dx() const { return read_register<u16>(RegisterDX); }
    u16 get_si() const { return read_register<u16>(RegisterSI); }
    u16 get_di() const { return read_register<u16>(RegisterDI); }
    u16 get_sp() const { return read_register<u16>(RegisterSP); }
    u16 get_bp() const { return read_register<u16>(RegisterBP); }

    u8 get_al() const { return read_register<u8>(RegisterAL); }
    u8 get_bl() const { return read_register<u8>(RegisterBL); }
    u8 get_cl() const { return read_register<u8>(RegisterCL); }
    u8 get_dl() const { return read_register<u8>(RegisterDL); }
    u8 get_ah() const { return read_register<u8>(RegisterAH); }
    u8 get_bh() const { return read_register<u8>(RegisterBH); }
    u8 get_ch() const { return read_register<u8>(RegisterCH); }
    u8 get_dh() const { return read_register<u8>(RegisterDH); }

    void set_al(u8 value) { write_register<u8>(RegisterAL, value); }
    void set_bl(u8 value) { write_register<u8>(RegisterBL, value); }
    void set_cl(u8 value) { write_register<u8>(RegisterCL, value); }
    void set_dl(u8 value) { write_register<u8>(RegisterDL, value); }
    void set_ah(u8 value) { write_register<u8>(RegisterAH, value); }
    void set_bh(u8 value) { write_register<u8>(RegisterBH, value); }
    void set_ch(u8 value) { write_register<u8>(RegisterCH, value); }
    void set_dh(u8 value) { write_register<u8>(RegisterDH, value); }

    void set_ax(u16 value) { write_register<u16>(RegisterAX, value); }
    void set_bx(u16 value) { write_register<u16>(RegisterBX, value); }
    void set_cx(u16 value) { write_register<u16>(RegisterCX, value); }
    void set_dx(u16 value) { write_register<u16>(RegisterDX, value); }
    void set_sp(u16 value) { write_register<u16>(RegisterSP, value); }
    void set_bp(u16 value) { write_register<u16>(RegisterBP, value); }
    void set_si(u16 value) { write_register<u16>(RegisterSI, value); }
    void set_di(u16 value) { write_register<u16>(RegisterDI, value); }

    void set_eax(u32 value) { write_register<u32>(RegisterEAX, value); }
    void set_ebx(u32 value) { write_register<u32>(RegisterEBX, value); }
    void set_ecx(u32 value) { write_register<u32>(RegisterECX, value); }
    void set_edx(u32 value) { write_register<u32>(RegisterEDX, value); }
    void set_esp(u32 value) { write_register<u32>(RegisterESP, value); }
    void set_ebp(u32 value) { write_register<u32>(RegisterEBP, value); }
    void set_esi(u32 value) { write_register<u32>(RegisterESI, value); }
    void set_edi(u32 value) { write_register<u32>(RegisterEDI, value); }

    u32 get_cr0() const { return m_cr0; }
    u32 get_cr2() const { return m_cr2; }
    u32 get_cr3() const { return m_cr3; }
    u32 get_cr4() const { return m_cr4; }

    u32 getDR0() const { return m_dr0; }
    u32 getDR1() const { return m_dr1; }
    u32 getDR2() const { return m_dr2; }
    u32 getDR3() const { return m_dr3; }
    u32 getDR4() const { return m_dr4; }
    u32 getDR5() const { return m_dr5; }
    u32 getDR6() const { return m_dr6; }
    u32 getDR7() const { return m_dr7; }

    // Base CS:EIP is the start address of the currently executing instruction
    u16 get_base_cs() const { return m_base_cs; }
    u16 get_base_ip() const { return m_base_eip & 0xFFFF; }
    u32 get_base_eip() const { return m_base_eip; }

    u32 current_stack_pointer() const
    {
        if (s32())
            return get_esp();
        return get_sp();
    }
    u32 current_base_pointer() const
    {
        if (s32())
            return get_ebp();
        return get_bp();
    }
    void set_current_stack_pointer(u32 value)
    {
        if (s32())
            set_esp(value);
        else
            set_sp(value);
    }
    void set_current_base_pointer(u32 value)
    {
        if (s32())
            set_ebp(value);
        else
            set_bp(value);
    }
    void adjust_stack_pointer(int delta)
    {
        set_current_stack_pointer(current_stack_pointer() + delta);
    }
    u32 current_instruction_pointer() const
    {
        return x32() ? get_eip() : get_ip();
    }
    u32 current_base_instruction_pointer() const
    {
        return x32() ? get_base_eip() : get_base_ip();
    }
    void adjust_instruction_pointer(int delta)
    {
        m_eip += delta;
    }

    void far_return(u16 stack_adjustment = 0);
    void real_mode_far_return(u16 stack_adjustment);
    void protected_far_return(u16 stack_adjustment);
    void protected_iret(TransactionalPopper&, LogicalAddress);
    void clear_segment_register_after_return_if_needed(SegmentRegisterIndex, JumpType);

    void real_mode_far_jump(LogicalAddress, JumpType);
    void protected_mode_far_jump(LogicalAddress, JumpType, Gate* = nullptr);
    void far_jump(LogicalAddress, JumpType, Gate* = nullptr);
    void jump_relative8(i8 displacement);
    void jump_relative16(i16 displacement);
    void jump_relative32(i32 displacement);
    void jump_absolute16(u16 offset);
    void jump_absolute32(u32 offset);

    void decodeNext();
    void execute(Instruction&);

    void execute_one_instruction();

    // CPU main loop - will fetch & decode until stopped
    void main_loop();
    bool main_loop_slow_stuff();

    // CPU main loop when halted (HLT) - will do nothing until an IRQ is raised
    void halted_loop();

    void push32(u32 value);
    u32 pop32();
    void push16(u16 value);
    u16 pop16();

    template<typename T>
    T pop();
    template<typename T>
    void push(T);

    void push_value_with_size(u32 value, ValueSize size)
    {
        if (size == WordSize)
            push16(value);
        else
            push32(value);
    }
    void push_operand_sized_value(u32 value)
    {
        if (o16())
            push16(value);
        else
            push32(value);
    }
    u32 pop_operand_sized_value() { return o16() ? pop16() : pop32(); }

    void push_segment_register_value(u16);

    Debugger& debugger() { return *m_debugger; }

    template<typename T>
    T in(u16 port);
    template<typename T>
    void out(u16 port, T data);

    u8 in8(u16 port);
    u16 in16(u16 port);
    u32 in32(u16 port);
    void out8(u16 port, u8 value);
    void out16(u16 port, u16 value);
    void out32(u16 port, u32 value);

    const u8* memory_pointer(LinearAddress);
    const u8* memory_pointer(LogicalAddress);
    const u8* memory_pointer(SegmentRegisterIndex, u32 offset);
    const u8* memory_pointer(const SegmentDescriptor&, u32 offset);

    u32 get_eflags() const;
    u16 get_flags() const;
    void set_eflags(u32 flags);
    void set_flags(u16 flags);
    void set_eflags_respectfully(u32 flags, u8 effectiveCPL);

    bool evaluate(u8) const;

    template<typename T>
    void update_flags(T);
    void update_flags32(u32 value);
    void update_flags16(u16 value);
    void update_flags8(u8 value);
    template<typename T>
    void math_flags(typename TypeDoubler<T>::type result, T dest, T src);
    template<typename T>
    void cmp_flags(typename TypeDoubler<T>::type result, T dest, T src);

    void adjust_flag(u64 result, u32 src, u32 dest)
    {
        set_af((((result ^ (src ^ dest)) & 0x10) >> 4) & 1);
    }

    template<typename T>
    T read_register(int register_index) const;
    template<typename T>
    void write_register(int register_index, T value);

    u32 read_register_for_address_size(int register_index);
    void write_register_for_address_size(int register_index, u32);
    void step_register_for_address_size(int register_index, u32 stepSize);
    bool decrement_cx_for_address_size();

    template<typename T>
    LogicalAddress read_logical_address(SegmentRegisterIndex, u32 offset);

    template<typename T>
    bool validate_physical_address(PhysicalAddress, MemoryAccessType);
    template<typename T>
    void validate_address(const SegmentDescriptor&, u32 offset, MemoryAccessType);
    template<typename T>
    void validate_address(SegmentRegisterIndex, u32 offset, MemoryAccessType);
    template<typename T>
    T read_physical_memory(PhysicalAddress);
    template<typename T>
    void write_physical_memory(PhysicalAddress, T);
    const u8* pointer_to_physical_memory(PhysicalAddress);
    template<typename T>
    T read_memory_metal(LinearAddress address);
    template<typename T>
    T read_memory(LinearAddress address, MemoryAccessType access_type = MemoryAccessType::Read, u8 effectiveCPL = 0xff);
    template<typename T>
    T read_memory(const SegmentDescriptor&, u32 offset, MemoryAccessType access_type = MemoryAccessType::Read);
    template<typename T>
    T read_memory(SegmentRegisterIndex, u32 offset, MemoryAccessType access_type = MemoryAccessType::Read);
    template<typename T>
    void write_memory_metal(LinearAddress, T);
    template<typename T>
    void write_memory(LinearAddress, T, u8 effectiveCPL = 0xff);
    template<typename T>
    void write_memory(const SegmentDescriptor&, u32 offset, T);
    template<typename T>
    void write_memory(SegmentRegisterIndex, u32 offset, T);

    PhysicalAddress translate_address(LinearAddress, MemoryAccessType, u8 effectiveCPL = 0xff);
    void snoop(LinearAddress, MemoryAccessType);
    void snoop(SegmentRegisterIndex, u32 offset, MemoryAccessType);

    template<typename T>
    void validate_io_access(u16 port);

    u8 read_memory8(LinearAddress);
    u8 read_memory8(SegmentRegisterIndex, u32 offset);
    u16 read_memory16(LinearAddress);
    u16 read_memory16(SegmentRegisterIndex, u32 offset);
    u32 read_memory32(LinearAddress);
    u32 read_memory32(SegmentRegisterIndex, u32 offset);
    u16 read_memory_metal16(LinearAddress);
    u32 read_memory_metal32(LinearAddress);
    void write_memory8(LinearAddress, u8);
    void write_memory8(SegmentRegisterIndex, u32 offset, u8 data);
    void write_memory16(LinearAddress, u16);
    void write_memory16(SegmentRegisterIndex, u32 offset, u16 data);
    void write_memory32(LinearAddress, u32);
    void write_memory32(SegmentRegisterIndex, u32 offset, u32 data);
    void write_memory_metal16(LinearAddress, u16);
    void write_memory_metal32(LinearAddress, u32);

    enum State {
        Dead,
        Alive,
        Halted
    };
    State state() const { return m_state; }
    void set_state(State s) { m_state = s; }

    SegmentDescriptor& cached_descriptor(SegmentRegisterIndex index) { return m_descriptor[(int)index]; }
    const SegmentDescriptor& cached_descriptor(SegmentRegisterIndex index) const { return m_descriptor[(int)index]; }

    // Dumps registers, flags & stack
    void dump_all();
    void dump_stack(ValueSize, unsigned count);
    void dump_watches();

    void dump_ivt();
    void dump_idt();
    void dump_ldt();
    void dump_gdt();

    void dump_memory(LogicalAddress, int rows);
    void dump_flat_memory(u32 address);
    void dump_raw_memory(u8*);
    unsigned dump_disassembled(LogicalAddress, unsigned count = 1);

    void dump_memory(SegmentDescriptor&, u32 offset, int rows);
    unsigned dump_disassembled(SegmentDescriptor&, u32 offset, unsigned count = 1);
    unsigned dump_disassembled_internal(SegmentDescriptor&, u32 offset);

    void dump_tss(const TSS&);

#ifdef CT_TRACE
    // Dumps registers (used by --trace)
    void dump_trace();
#endif

    QVector<WatchedAddress>& watches()
    {
        return m_watches;
    }

    // Current execution mode (16 or 32 bit)
    bool x16() const { return !x32(); }
    bool x32() const { return cached_descriptor(SegmentRegisterIndex::CS).d(); }

    bool a16() const { return !m_effective_address_size32; }
    bool a32() const { return m_effective_address_size32; }
    bool o16() const { return !m_effective_operand_size32; }
    bool o32() const { return m_effective_operand_size32; }

    bool s16() const { return !m_stackSize32; }
    bool s32() const { return m_stackSize32; }

    enum Command {
        ExitDebugger,
        EnterDebugger,
        HardReboot
    };
    void queue_command(Command);

    static const char* register_name(CPU::RegisterIndex8) PURE;
    static const char* register_name(CPU::RegisterIndex16) PURE;
    static const char* register_name(CPU::RegisterIndex32) PURE;
    static const char* register_name(SegmentRegisterIndex) PURE;

protected:
    void _CPUID(Instruction&);
    void _ESCAPE(Instruction&);
    void _WAIT(Instruction&);
    void _NOP(Instruction&);
    void _HLT(Instruction&);
    void _INT_imm8(Instruction&);
    void _INT3(Instruction&);
    void _INTO(Instruction&);
    void _IRET(Instruction&);

    void _AAA(Instruction&);
    void _AAM(Instruction&);
    void _AAD(Instruction&);
    void _AAS(Instruction&);

    void _DAA(Instruction&);
    void _DAS(Instruction&);

    void _STC(Instruction&);
    void _STD(Instruction&);
    void _STI(Instruction&);
    void _CLC(Instruction&);
    void _CLD(Instruction&);
    void _CLI(Instruction&);
    void _CMC(Instruction&);
    void _CLTS(Instruction&);
    void _LAR_reg16_RM16(Instruction&);
    void _LAR_reg32_RM32(Instruction&);
    void _LSL_reg16_RM16(Instruction&);
    void _LSL_reg32_RM32(Instruction&);
    void _VERR_RM16(Instruction&);
    void _VERW_RM16(Instruction&);
    void _ARPL(Instruction&);

    void _WBINVD(Instruction&);
    void _INVLPG(Instruction&);

    void _CBW(Instruction&);
    void _CWD(Instruction&);
    void _CWDE(Instruction&);
    void _CDQ(Instruction&);

    void _XLAT(Instruction&);
    void _SALC(Instruction&);

    void _JMP_imm32(Instruction&);
    void _JMP_imm16(Instruction&);
    void _JMP_imm16_imm16(Instruction&);
    void _JMP_short_imm8(Instruction&);
    void _JCXZ_imm8(Instruction&);

    void _Jcc_imm8(Instruction&);
    void _Jcc_NEAR_imm(Instruction&);
    void _SETcc_RM8(Instruction&);
    void _CMOVcc_reg16_RM16(Instruction&);
    void _CMOVcc_reg32_RM32(Instruction&);

    void _CALL_imm16(Instruction&);
    void _CALL_imm32(Instruction&);
    void _RET(Instruction&);
    void _RET_imm16(Instruction&);
    void _RETF(Instruction&);
    void _RETF_imm16(Instruction&);

    void doLOOP(Instruction&, bool condition);
    void _LOOP_imm8(Instruction&);
    void _LOOPZ_imm8(Instruction&);
    void _LOOPNZ_imm8(Instruction&);

    void _XCHG_AX_reg16(Instruction&);
    void _XCHG_EAX_reg32(Instruction&);
    void _XCHG_reg8_RM8(Instruction&);
    void _XCHG_reg16_RM16(Instruction&);
    void _XCHG_reg32_RM32(Instruction&);

    template<typename F>
    void doOnceOrRepeatedly(Instruction&, bool careAboutZF, F);
    template<typename T>
    void doLODS(Instruction&);
    template<typename T>
    void doSTOS(Instruction&);
    template<typename T>
    void doMOVS(Instruction&);
    template<typename T>
    void doINS(Instruction&);
    template<typename T>
    void doOUTS(Instruction&);
    template<typename T>
    void doCMPS(Instruction&);
    template<typename T>
    void doSCAS(Instruction&);

    void _CMPXCHG_RM32_reg32(Instruction&);
    void _CMPXCHG_RM16_reg16(Instruction&);

    void _CMPSB(Instruction&);
    void _CMPSW(Instruction&);
    void _CMPSD(Instruction&);
    void _LODSB(Instruction&);
    void _LODSW(Instruction&);
    void _LODSD(Instruction&);
    void _SCASB(Instruction&);
    void _SCASW(Instruction&);
    void _SCASD(Instruction&);
    void _STOSB(Instruction&);
    void _STOSW(Instruction&);
    void _STOSD(Instruction&);
    void _MOVSB(Instruction&);
    void _MOVSW(Instruction&);
    void _MOVSD(Instruction&);

    void _VKILL(Instruction&);

    void _LEA_reg16_mem16(Instruction&);
    void _LEA_reg32_mem32(Instruction&);

    void _LDS_reg16_mem16(Instruction&);
    void _LDS_reg32_mem32(Instruction&);
    void _LES_reg16_mem16(Instruction&);
    void _LES_reg32_mem32(Instruction&);

    void _MOV_reg8_imm8(Instruction&);
    void _MOV_reg16_imm16(Instruction&);
    void _MOV_reg32_imm32(Instruction&);

    template<typename T>
    void doMOV_moff_Areg(Instruction&);
    template<typename T>
    void doMOV_Areg_moff(Instruction&);

    void _MOV_seg_RM16(Instruction&);
    void _MOV_RM16_seg(Instruction&);
    void _MOV_AL_moff8(Instruction&);
    void _MOV_AX_moff16(Instruction&);
    void _MOV_EAX_moff32(Instruction&);
    void _MOV_moff8_AL(Instruction&);
    void _MOV_moff16_AX(Instruction&);
    void _MOV_reg8_RM8(Instruction&);
    void _MOV_reg16_RM16(Instruction&);
    void _MOV_RM8_reg8(Instruction&);
    void _MOV_RM16_reg16(Instruction&);
    void _MOV_RM8_imm8(Instruction&);
    void _MOV_RM16_imm16(Instruction&);
    void _MOV_RM32_imm32(Instruction&);

    void _XOR_RM8_reg8(Instruction&);
    void _XOR_RM16_reg16(Instruction&);
    void _XOR_reg8_RM8(Instruction&);
    void _XOR_reg16_RM16(Instruction&);
    void _XOR_reg32_RM32(Instruction&);
    void _XOR_RM8_imm8(Instruction&);
    void _XOR_RM16_imm16(Instruction&);
    void _XOR_RM16_imm8(Instruction&);
    void _XOR_AL_imm8(Instruction&);
    void _XOR_AX_imm16(Instruction&);
    void _XOR_EAX_imm32(Instruction&);

    void _OR_RM8_reg8(Instruction&);
    void _OR_RM16_reg16(Instruction&);
    void _OR_RM32_reg32(Instruction&);
    void _OR_reg8_RM8(Instruction&);
    void _OR_reg16_RM16(Instruction&);
    void _OR_reg32_RM32(Instruction&);
    void _OR_RM8_imm8(Instruction&);
    void _OR_RM16_imm16(Instruction&);
    void _OR_RM16_imm8(Instruction&);
    void _OR_EAX_imm32(Instruction&);
    void _OR_AX_imm16(Instruction&);
    void _OR_AL_imm8(Instruction&);

    void _AND_RM8_reg8(Instruction&);
    void _AND_RM16_reg16(Instruction&);
    void _AND_reg8_RM8(Instruction&);
    void _AND_reg16_RM16(Instruction&);
    void _AND_RM8_imm8(Instruction&);
    void _AND_RM16_imm16(Instruction&);
    void _AND_RM16_imm8(Instruction&);
    void _AND_AL_imm8(Instruction&);
    void _AND_AX_imm16(Instruction&);
    void _AND_EAX_imm32(Instruction&);

    void _TEST_RM8_reg8(Instruction&);
    void _TEST_RM16_reg16(Instruction&);
    void _TEST_RM32_reg32(Instruction&);
    void _TEST_AL_imm8(Instruction&);
    void _TEST_AX_imm16(Instruction&);
    void _TEST_EAX_imm32(Instruction&);

    void _PUSH_SP_8086_80186(Instruction&);
    void _PUSH_CS(Instruction&);
    void _PUSH_DS(Instruction&);
    void _PUSH_ES(Instruction&);
    void _PUSH_SS(Instruction&);
    void _PUSHF(Instruction&);

    void _POP_DS(Instruction&);
    void _POP_ES(Instruction&);
    void _POP_SS(Instruction&);
    void _POPF(Instruction&);

    void _LAHF(Instruction&);
    void _SAHF(Instruction&);

    void _OUT_imm8_AL(Instruction&);
    void _OUT_imm8_AX(Instruction&);
    void _OUT_imm8_EAX(Instruction&);
    void _OUT_DX_AL(Instruction&);
    void _OUT_DX_AX(Instruction&);
    void _OUT_DX_EAX(Instruction&);
    void _OUTSB(Instruction&);
    void _OUTSW(Instruction&);
    void _OUTSD(Instruction&);

    void _IN_AL_imm8(Instruction&);
    void _IN_AX_imm8(Instruction&);
    void _IN_EAX_imm8(Instruction&);
    void _IN_AL_DX(Instruction&);
    void _IN_AX_DX(Instruction&);
    void _IN_EAX_DX(Instruction&);
    void _INSB(Instruction&);
    void _INSW(Instruction&);
    void _INSD(Instruction&);

    void _ADD_RM8_reg8(Instruction&);
    void _ADD_RM16_reg16(Instruction&);
    void _ADD_reg8_RM8(Instruction&);
    void _ADD_reg16_RM16(Instruction&);
    void _ADD_AL_imm8(Instruction&);
    void _ADD_AX_imm16(Instruction&);
    void _ADD_EAX_imm32(Instruction&);
    void _ADD_RM8_imm8(Instruction&);
    void _ADD_RM16_imm16(Instruction&);
    void _ADD_RM16_imm8(Instruction&);

    void _SUB_RM8_reg8(Instruction&);
    void _SUB_RM16_reg16(Instruction&);
    void _SUB_reg8_RM8(Instruction&);
    void _SUB_reg16_RM16(Instruction&);
    void _SUB_AL_imm8(Instruction&);
    void _SUB_AX_imm16(Instruction&);
    void _SUB_EAX_imm32(Instruction&);
    void _SUB_RM8_imm8(Instruction&);
    void _SUB_RM16_imm16(Instruction&);
    void _SUB_RM16_imm8(Instruction&);

    void _ADC_RM8_reg8(Instruction&);
    void _ADC_RM16_reg16(Instruction&);
    void _ADC_reg8_RM8(Instruction&);
    void _ADC_reg16_RM16(Instruction&);
    void _ADC_AL_imm8(Instruction&);
    void _ADC_AX_imm16(Instruction&);
    void _ADC_EAX_imm32(Instruction&);
    void _ADC_RM8_imm8(Instruction&);
    void _ADC_RM16_imm16(Instruction&);
    void _ADC_RM16_imm8(Instruction&);

    void _XADD_RM16_reg16(Instruction&);
    void _XADD_RM32_reg32(Instruction&);
    void _XADD_RM8_reg8(Instruction&);

    void _SBB_RM8_reg8(Instruction&);
    void _SBB_RM16_reg16(Instruction&);
    void _SBB_RM32_reg32(Instruction&);
    void _SBB_reg8_RM8(Instruction&);
    void _SBB_reg16_RM16(Instruction&);
    void _SBB_AL_imm8(Instruction&);
    void _SBB_AX_imm16(Instruction&);
    void _SBB_EAX_imm32(Instruction&);
    void _SBB_RM8_imm8(Instruction&);
    void _SBB_RM16_imm16(Instruction&);
    void _SBB_RM16_imm8(Instruction&);

    void _CMP_RM8_reg8(Instruction&);
    void _CMP_RM16_reg16(Instruction&);
    void _CMP_RM32_reg32(Instruction&);
    void _CMP_reg8_RM8(Instruction&);
    void _CMP_reg16_RM16(Instruction&);
    void _CMP_reg32_RM32(Instruction&);
    void _CMP_AL_imm8(Instruction&);
    void _CMP_AX_imm16(Instruction&);
    void _CMP_EAX_imm32(Instruction&);
    void _CMP_RM8_imm8(Instruction&);
    void _CMP_RM16_imm16(Instruction&);
    void _CMP_RM16_imm8(Instruction&);

    void _MUL_RM8(Instruction&);
    void _MUL_RM16(Instruction&);
    void _MUL_RM32(Instruction&);
    void _DIV_RM8(Instruction&);
    void _DIV_RM16(Instruction&);
    void _DIV_RM32(Instruction&);
    void _IMUL_RM8(Instruction&);
    void _IMUL_RM16(Instruction&);
    void _IMUL_RM32(Instruction&);
    void _IDIV_RM8(Instruction&);
    void _IDIV_RM16(Instruction&);
    void _IDIV_RM32(Instruction&);

    void _TEST_RM8_imm8(Instruction&);
    void _TEST_RM16_imm16(Instruction&);

    template<typename T>
    void doNEG(Instruction&);
    template<typename T>
    void doNOT(Instruction&);
    void _NOT_RM8(Instruction&);
    void _NOT_RM16(Instruction&);
    void _NOT_RM32(Instruction&);
    void _NEG_RM8(Instruction&);
    void _NEG_RM16(Instruction&);
    void _NEG_RM32(Instruction&);

    template<typename T, class Accessor>
    void doDEC(Accessor);
    template<typename T, class Accessor>
    void doINC(Accessor);

    void _INC_RM8(Instruction&);
    void _INC_RM16(Instruction&);
    void _INC_RM32(Instruction&);
    void _INC_reg16(Instruction&);
    void _INC_reg32(Instruction&);
    void _DEC_RM8(Instruction&);
    void _DEC_RM16(Instruction&);
    void _DEC_RM32(Instruction&);
    void _DEC_reg16(Instruction&);
    void _DEC_reg32(Instruction&);

    template<typename T>
    void doFarJump(Instruction&, JumpType);

    void _CALL_RM16(Instruction&);
    void _CALL_RM32(Instruction&);
    void _CALL_FAR_mem16(Instruction&);
    void _CALL_FAR_mem32(Instruction&);
    void _CALL_imm16_imm16(Instruction&);
    void _CALL_imm16_imm32(Instruction&);

    void _JMP_RM16(Instruction&);
    void _JMP_RM32(Instruction&);
    void _JMP_FAR_mem16(Instruction&);
    void _JMP_FAR_mem32(Instruction&);

    void _PUSH_RM16(Instruction&);
    void _PUSH_RM32(Instruction&);
    void _POP_RM16(Instruction&);
    void _POP_RM32(Instruction&);

    void _wrap_0xC0(Instruction&);
    void _wrap_0xC1_16(Instruction&);
    void _wrap_0xC1_32(Instruction&);
    void _wrap_0xD0(Instruction&);
    void _wrap_0xD1_16(Instruction&);
    void _wrap_0xD1_32(Instruction&);
    void _wrap_0xD2(Instruction&);
    void _wrap_0xD3_16(Instruction&);
    void _wrap_0xD3_32(Instruction&);

    template<typename T>
    void doBOUND(Instruction&);
    void _BOUND(Instruction&);

    template<typename T>
    void doENTER(Instruction&);
    template<typename T>
    void doLEAVE();
    void _ENTER16(Instruction&);
    void _ENTER32(Instruction&);
    void _LEAVE16(Instruction&);
    void _LEAVE32(Instruction&);

    template<typename T>
    void doPUSHA();
    template<typename T>
    void doPOPA();
    void _PUSHA(Instruction&);
    void _POPA(Instruction&);
    void _PUSH_imm8(Instruction&);
    void _PUSH_imm16(Instruction&);

    void _IMUL_reg16_RM16(Instruction&);
    void _IMUL_reg32_RM32(Instruction&);
    void _IMUL_reg16_RM16_imm8(Instruction&);
    void _IMUL_reg32_RM32_imm8(Instruction&);
    void _IMUL_reg16_RM16_imm16(Instruction&);
    void _IMUL_reg32_RM32_imm32(Instruction&);

    void _LMSW_RM16(Instruction&);
    void _SMSW_RM16(Instruction&);

    void doLGDTorLIDT(Instruction&, DescriptorTableRegister&);
    void doSGDTorSIDT(Instruction&, DescriptorTableRegister&);

    void _SGDT(Instruction&);
    void _LGDT(Instruction&);
    void _SIDT(Instruction&);
    void _LIDT(Instruction&);
    void _LLDT_RM16(Instruction&);
    void _SLDT_RM16(Instruction&);
    void _LTR_RM16(Instruction&);
    void _STR_RM16(Instruction&);

    void _PUSHAD(Instruction&);
    void _POPAD(Instruction&);
    void _PUSHFD(Instruction&);
    void _POPFD(Instruction&);
    void _PUSH_imm32(Instruction&);

    void _PUSH_reg16(Instruction&);
    void _PUSH_reg32(Instruction&);
    void _POP_reg16(Instruction&);
    void _POP_reg32(Instruction&);

    void _TEST_RM32_imm32(Instruction&);
    void _XOR_RM32_reg32(Instruction&);
    void _ADD_RM32_reg32(Instruction&);
    void _ADC_RM32_reg32(Instruction&);
    void _SUB_RM32_reg32(Instruction&);

    template<typename op, typename T>
    void _BTx_RM_reg(Instruction&);
    template<typename op, typename T>
    void _BTx_RM_imm8(Instruction&);

    template<typename op>
    void _BTx_RM32_reg32(Instruction&);
    template<typename op>
    void _BTx_RM16_reg16(Instruction&);
    template<typename op>
    void _BTx_RM32_imm8(Instruction&);
    template<typename op>
    void _BTx_RM16_imm8(Instruction&);

    void _BT_RM16_imm8(Instruction&);
    void _BT_RM32_imm8(Instruction&);
    void _BT_RM16_reg16(Instruction&);
    void _BT_RM32_reg32(Instruction&);
    void _BTR_RM16_imm8(Instruction&);
    void _BTR_RM32_imm8(Instruction&);
    void _BTR_RM16_reg16(Instruction&);
    void _BTR_RM32_reg32(Instruction&);
    void _BTC_RM16_imm8(Instruction&);
    void _BTC_RM32_imm8(Instruction&);
    void _BTC_RM16_reg16(Instruction&);
    void _BTC_RM32_reg32(Instruction&);
    void _BTS_RM16_imm8(Instruction&);
    void _BTS_RM32_imm8(Instruction&);
    void _BTS_RM16_reg16(Instruction&);
    void _BTS_RM32_reg32(Instruction&);

    void _BSF_reg16_RM16(Instruction&);
    void _BSF_reg32_RM32(Instruction&);
    void _BSR_reg16_RM16(Instruction&);
    void _BSR_reg32_RM32(Instruction&);

    void _ROL_RM8_imm8(Instruction&);
    void _ROL_RM16_imm8(Instruction&);
    void _ROL_RM32_imm8(Instruction&);
    void _ROL_RM8_1(Instruction&);
    void _ROL_RM16_1(Instruction&);
    void _ROL_RM32_1(Instruction&);
    void _ROL_RM8_CL(Instruction&);
    void _ROL_RM16_CL(Instruction&);
    void _ROL_RM32_CL(Instruction&);

    void _ROR_RM8_imm8(Instruction&);
    void _ROR_RM16_imm8(Instruction&);
    void _ROR_RM32_imm8(Instruction&);
    void _ROR_RM8_1(Instruction&);
    void _ROR_RM16_1(Instruction&);
    void _ROR_RM32_1(Instruction&);
    void _ROR_RM8_CL(Instruction&);
    void _ROR_RM16_CL(Instruction&);
    void _ROR_RM32_CL(Instruction&);

    void _SHL_RM8_imm8(Instruction&);
    void _SHL_RM16_imm8(Instruction&);
    void _SHL_RM32_imm8(Instruction&);
    void _SHL_RM8_1(Instruction&);
    void _SHL_RM16_1(Instruction&);
    void _SHL_RM32_1(Instruction&);
    void _SHL_RM8_CL(Instruction&);
    void _SHL_RM16_CL(Instruction&);
    void _SHL_RM32_CL(Instruction&);

    void _SHR_RM8_imm8(Instruction&);
    void _SHR_RM16_imm8(Instruction&);
    void _SHR_RM32_imm8(Instruction&);
    void _SHR_RM8_1(Instruction&);
    void _SHR_RM16_1(Instruction&);
    void _SHR_RM32_1(Instruction&);
    void _SHR_RM8_CL(Instruction&);
    void _SHR_RM16_CL(Instruction&);
    void _SHR_RM32_CL(Instruction&);

    void _SAR_RM8_imm8(Instruction&);
    void _SAR_RM16_imm8(Instruction&);
    void _SAR_RM32_imm8(Instruction&);
    void _SAR_RM8_1(Instruction&);
    void _SAR_RM16_1(Instruction&);
    void _SAR_RM32_1(Instruction&);
    void _SAR_RM8_CL(Instruction&);
    void _SAR_RM16_CL(Instruction&);
    void _SAR_RM32_CL(Instruction&);

    void _RCL_RM8_imm8(Instruction&);
    void _RCL_RM16_imm8(Instruction&);
    void _RCL_RM32_imm8(Instruction&);
    void _RCL_RM8_1(Instruction&);
    void _RCL_RM16_1(Instruction&);
    void _RCL_RM32_1(Instruction&);
    void _RCL_RM8_CL(Instruction&);
    void _RCL_RM16_CL(Instruction&);
    void _RCL_RM32_CL(Instruction&);

    void _RCR_RM8_imm8(Instruction&);
    void _RCR_RM16_imm8(Instruction&);
    void _RCR_RM32_imm8(Instruction&);
    void _RCR_RM8_1(Instruction&);
    void _RCR_RM16_1(Instruction&);
    void _RCR_RM32_1(Instruction&);
    void _RCR_RM8_CL(Instruction&);
    void _RCR_RM16_CL(Instruction&);
    void _RCR_RM32_CL(Instruction&);

    void _SHLD_RM16_reg16_imm8(Instruction&);
    void _SHLD_RM32_reg32_imm8(Instruction&);
    void _SHLD_RM16_reg16_CL(Instruction&);
    void _SHLD_RM32_reg32_CL(Instruction&);
    void _SHRD_RM16_reg16_imm8(Instruction&);
    void _SHRD_RM32_reg32_imm8(Instruction&);
    void _SHRD_RM16_reg16_CL(Instruction&);
    void _SHRD_RM32_reg32_CL(Instruction&);

    void _MOVZX_reg16_RM8(Instruction&);
    void _MOVZX_reg32_RM8(Instruction&);
    void _MOVZX_reg32_RM16(Instruction&);

    void _MOVSX_reg16_RM8(Instruction&);
    void _MOVSX_reg32_RM8(Instruction&);
    void _MOVSX_reg32_RM16(Instruction&);

    void _BSWAP_reg32(Instruction&);

    template<typename T>
    void doLxS(Instruction&, SegmentRegisterIndex);
    void _LFS_reg16_mem16(Instruction&);
    void _LFS_reg32_mem32(Instruction&);
    void _LGS_reg16_mem16(Instruction&);
    void _LGS_reg32_mem32(Instruction&);
    void _LSS_reg16_mem16(Instruction&);
    void _LSS_reg32_mem32(Instruction&);

    void _PUSH_FS(Instruction&);
    void _PUSH_GS(Instruction&);
    void _POP_FS(Instruction&);
    void _POP_GS(Instruction&);

    void _MOV_RM32_reg32(Instruction&);
    void _MOV_reg32_RM32(Instruction&);
    void _MOV_reg32_CR(Instruction&);
    void _MOV_CR_reg32(Instruction&);
    void _MOV_reg32_DR(Instruction&);
    void _MOV_DR_reg32(Instruction&);
    void _MOV_moff32_EAX(Instruction&);

    void _MOV_seg_RM32(Instruction&);

    void _JMP_imm16_imm32(Instruction&);

    void _ADD_RM32_imm32(Instruction&);
    void _OR_RM32_imm32(Instruction&);
    void _ADC_RM32_imm32(Instruction&);
    void _SBB_RM32_imm32(Instruction&);
    void _AND_RM32_imm32(Instruction&);
    void _SUB_RM32_imm32(Instruction&);
    void _XOR_RM32_imm32(Instruction&);
    void _CMP_RM32_imm32(Instruction&);

    void _ADD_RM32_imm8(Instruction&);
    void _OR_RM32_imm8(Instruction&);
    void _ADC_RM32_imm8(Instruction&);
    void _SBB_RM32_imm8(Instruction&);
    void _AND_RM32_imm8(Instruction&);
    void _SUB_RM32_imm8(Instruction&);
    void _XOR_RM32_imm8(Instruction&);
    void _CMP_RM32_imm8(Instruction&);

    void _ADD_reg32_RM32(Instruction&);
    void _ADC_reg32_RM32(Instruction&);
    void _SBB_reg32_RM32(Instruction&);
    void _AND_reg32_RM32(Instruction&);
    void _SUB_reg32_RM32(Instruction&);
    void _AND_RM32_reg32(Instruction&);

    void _RDTSC(Instruction&);

    void _UD0(Instruction&);
    void _UD1(Instruction&);
    void _UD2(Instruction&);

private:
    friend class Instruction;
    friend class InstructionExecutionContext;

    template<typename T>
    T read_instruction_stream();
    u8 read_instruction8() override;
    u16 read_instruction16() override;
    u32 read_instruction32() override;

    void init_watches();
    void hard_reboot();

    void update_default_sizes();
    void update_stack_size();
    void update_code_segment_cache();
    void make_next_instruction_uninterruptible();

    PhysicalAddress translate_address_slow_case(LinearAddress, MemoryAccessType, u8 effectiveCPL);

    template<typename T>
    T doSAR(T, unsigned steps);
    template<typename T>
    T doRCL(T, unsigned steps);
    template<typename T>
    T doRCR(T, unsigned steps);

    template<typename T>
    T doSHL(T, unsigned steps);
    template<typename T>
    T doSHR(T, unsigned steps);

    template<typename T>
    T doSHLD(T, T, unsigned steps);
    template<typename T>
    T doSHRD(T, T, unsigned steps);

    template<typename T>
    T doROL(T, unsigned steps);
    template<typename T>
    T doROR(T, unsigned steps);

    template<typename T>
    T doXOR(T, T);
    template<typename T>
    T doOR(T, T);
    template<typename T>
    T doAND(T, T);

    template<typename T>
    T doBSF(T);
    template<typename T>
    T doBSR(T);

    template<typename T>
    u64 doADD(T, T);
    template<typename T>
    u64 doADC(T, T);
    template<typename T>
    u64 doSUB(T, T);
    template<typename T>
    u64 doSBB(T, T);
    template<typename T>
    void doIMUL(T f1, T f2, T& resultHigh, T& resultLow);
    template<typename T>
    void doMUL(T f1, T f2, T& resultHigh, T& resultLow);
    template<typename T>
    void doDIV(T dividendHigh, T dividendLow, T divisor, T& quotient, T& remainder);
    template<typename T>
    void doIDIV(T dividendHigh, T dividendLow, T divisor, T& quotient, T& remainder);

    void save_base_address()
    {
        m_base_cs = get_cs();
        m_base_eip = get_eip();
    }

    void set_ldt(u16 segment);
    void task_switch(u16 task, JumpType);
    void task_switch(u16 task_selector, TSSDescriptor&, JumpType);
    TSS current_tss();

    void write_to_gdt(Descriptor&);

    void dump_selector(const char* prefix, SegmentRegisterIndex);
    void write_segment_register(SegmentRegisterIndex, u16 selector);
    void validate_segment_load(SegmentRegisterIndex, u16 selector, const Descriptor&);

    SegmentDescriptor m_descriptor[6];

    PartAddressableRegister m_gpr[8];
    u8* m_byte_registers[8];

    u32 m_eip { 0 };

    u16 m_cs, m_ds, m_es, m_ss, m_fs, m_gs;
    mutable bool m_cf, m_pf, m_af, m_zf, m_sf, m_of;
    bool m_df, m_if, m_tf;

    unsigned int m_iopl;
    bool m_nt;
    bool m_rf;
    bool m_vm;
    bool m_ac;
    bool m_vif;
    bool m_vip;
    bool m_id;

    DescriptorTableRegister m_gdtr { "GDT" };
    DescriptorTableRegister m_idtr { "IDT" };
    DescriptorTableRegister m_ldtr { "LDT" };

    u32 m_cr0 { 0 };
    u32 m_cr2 { 0 };
    u32 m_cr3 { 0 };
    u32 m_cr4 { 0 };

    u32 m_dr0, m_dr1, m_dr2, m_dr3, m_dr4, m_dr5, m_dr6, m_dr7;

    struct {
        u16 selector { 0 };
        LinearAddress base { 0 };
        u16 limit { 0 };
        bool is_32bit { false };
    } m_tr;

    State m_state { Dead };

    // Actual CS:EIP (when we started fetching the instruction)
    u16 m_base_cs { 0 };
    u32 m_base_eip { 0 };

    SegmentRegisterIndex m_segment_prefix { SegmentRegisterIndex::None };

    u32 m_base_memory_size { 0 };
    u32 m_extended_memory_size { 0 };

    std::set<LogicalAddress> m_breakpoints;

    bool m_a20_enabled { false };
    bool m_next_instruction_is_uninterruptible { false };

    OwnPtr<Debugger> m_debugger;

    // One MemoryProvider* per 'memoryProviderBlockSize' bytes for the first MB of memory.
    static const size_t memory_provider_block_size = 16384;
    MemoryProvider* m_memory_providers[1048576 / memory_provider_block_size];

    u8* m_memory { nullptr };
    size_t m_memory_size { 0 };

    u16* m_segment_map[8];
    u32* m_control_register_map[8];
    u32* m_debug_register_map[8];

    Machine& m_machine;

    bool m_address_size32 { false };
    bool m_operand_size32 { false };
    bool m_effective_address_size32 { false };
    bool m_effective_operand_size32 { false };
    bool m_stackSize32 { false };

    enum DebuggerRequest {
        NoDebuggerRequest,
        PleaseEnterDebugger,
        PleaseExitDebugger
    };

    std::atomic<bool> m_main_loop_needs_slow_stuff { false };
    std::atomic<DebuggerRequest> m_debugger_request { NoDebuggerRequest };
    std::atomic<bool> m_should_hard_reboot { false };

    QVector<WatchedAddress> m_watches;

#ifdef SYMBOLIC_TRACING
    QHash<u32, QString> m_symbols;
    QHash<QString, u32> m_symbols_reverse;
#endif

#ifdef VMM_TRACING
    QVector<QString> m_vmm_names;
#endif

    bool m_is_for_autotest { false };

    u64 m_cycle { 0 };

    mutable u32 m_dirty_flags { 0 };
    u64 m_last_result { 0 };
    unsigned m_last_op_size { ByteSize };
};

extern CPU* g_cpu;

#include "debug.h"

ALWAYS_INLINE bool CPU::evaluate(u8 condition_code) const
{
    ASSERT(condition_code <= 0xF);

    switch (condition_code) {
    case 0:
        return this->m_of; // O
    case 1:
        return !this->m_of; // NO
    case 2:
        return this->m_cf; // B, C, NAE
    case 3:
        return !this->m_cf; // NB, NC, AE
    case 4:
        return get_zf(); // E, Z
    case 5:
        return !get_zf(); // NE, NZ
    case 6:
        return (this->m_cf | get_zf()); // BE, NA
    case 7:
        return !(this->m_cf | get_zf()); // NBE, A
    case 8:
        return get_sf(); // S
    case 9:
        return !get_sf(); // NS
    case 10:
        return get_pf(); // P, PE
    case 11:
        return !get_pf(); // NP, PO
    case 12:
        return get_sf() ^ this->m_of; // L, NGE
    case 13:
        return !(get_sf() ^ this->m_of); // NL, GE
    case 14:
        return (get_sf() ^ this->m_of) | get_zf(); // LE, NG
    case 15:
        return !((get_sf() ^ this->m_of) | get_zf()); // NLE, G
    }
    return 0;
}

ALWAYS_INLINE u8& Instruction::reg8()
{
#ifdef DEBUG_INSTRUCTION
    ASSERT(m_cpu);
#endif
    return *m_cpu->m_byte_registers[register_index()];
}

ALWAYS_INLINE u16& Instruction::reg16()
{
#ifdef DEBUG_INSTRUCTION
    ASSERT(m_cpu);
#endif
    return m_cpu->m_gpr[register_index()].low_u16;
}

ALWAYS_INLINE u16& Instruction::segreg()
{
#ifdef DEBUG_INSTRUCTION
    ASSERT(m_cpu);
    ASSERT(register_index() < 6);
#endif
    return *m_cpu->m_segment_map[register_index()];
}

ALWAYS_INLINE u32& Instruction::reg32()
{
#ifdef DEBUG_INSTRUCTION
    ASSERT(m_cpu);
    ASSERT(m_cpu->o32());
#endif
    return m_cpu->m_gpr[register_index()].full_u32;
}

template<>
ALWAYS_INLINE u8& Instruction::reg<u8>() { return reg8(); }
template<>
ALWAYS_INLINE u16& Instruction::reg<u16>() { return reg16(); }
template<>
ALWAYS_INLINE u32& Instruction::reg<u32>() { return reg32(); }

template<typename T>
inline void CPU::update_flags(T result)
{
    switch (TypeTrivia<T>::bits) {
    case 8:
        update_flags8(result);
        break;
    case 16:
        update_flags16(result);
        break;
    case 32:
        update_flags32(result);
        break;
    }
}

template<typename T>
inline T CPU::pop()
{
    if (sizeof(T) == 4)
        return pop32();
    return pop16();
}

template<typename T>
inline void CPU::push(T data)
{
    if (sizeof(T) == 4)
        push32(data);
    else
        push16(data);
}

template<typename T>
ALWAYS_INLINE T CPU::read_register(int register_index) const
{
    if (sizeof(T) == 1)
        return *m_byte_registers[register_index];
    if (sizeof(T) == 2)
        return m_gpr[register_index].low_u16;
    if (sizeof(T) == 4)
        return m_gpr[register_index].full_u32;
    ASSERT_NOT_REACHED();
}

template<typename T>
ALWAYS_INLINE void CPU::write_register(int register_index, T value)
{
    if (sizeof(T) == 1)
        *m_byte_registers[register_index] = value;
    else if (sizeof(T) == 2)
        m_gpr[register_index].low_u16 = value;
    else if (sizeof(T) == 4)
        m_gpr[register_index].full_u32 = value;
    else
        ASSERT_NOT_REACHED();
}

inline u32 MemoryOrRegisterReference::offset()
{
    ASSERT(!is_register());
    if (m_a32)
        return m_offset32;
    else
        return m_offset16;
}

template<typename T>
inline T MemoryOrRegisterReference::read()
{
    ASSERT(m_cpu);
    if (is_register())
        return m_cpu->read_register<T>(m_register_index);
    return m_cpu->read_memory<T>(segment(), offset());
}

template<typename T>
inline void MemoryOrRegisterReference::write(T data)
{
    ASSERT(m_cpu);
    if (is_register()) {
        m_cpu->write_register<T>(m_register_index, data);
        return;
    }
    m_cpu->write_memory<T>(segment(), offset(), data);
}

inline u8 MemoryOrRegisterReference::read8() { return read<u8>(); }
inline u16 MemoryOrRegisterReference::read16() { return read<u16>(); }
inline u32 MemoryOrRegisterReference::read32()
{
    ASSERT(m_cpu->o32());
    return read<u32>();
}
inline void MemoryOrRegisterReference::write8(u8 data) { return write(data); }
inline void MemoryOrRegisterReference::write16(u16 data) { return write(data); }
inline void MemoryOrRegisterReference::write32(u32 data)
{
    ASSERT(m_cpu->o32());
    return write(data);
}

template<typename T>
void CPU::math_flags(typename TypeDoubler<T>::type result, T dest, T src)
{
    typedef typename TypeDoubler<T>::type DT;
    m_dirty_flags |= Flag::PF | Flag::ZF | Flag::SF;
    m_last_result = result;
    m_last_op_size = TypeTrivia<T>::bits;

    set_cf(result & ((DT)TypeTrivia<T>::mask << TypeTrivia<T>::bits));
    adjust_flag(result, dest, src);
}

template<typename T>
inline void CPU::cmp_flags(typename TypeDoubler<T>::type result, T dest, T src)
{
    math_flags<T>(result, dest, src);
    set_of((((result ^ dest) & (src ^ dest)) >> (TypeTrivia<T>::bits - 1)) & 1);
}

ALWAYS_INLINE void Instruction::execute(CPU& cpu)
{
    m_cpu = &cpu;
    cpu.set_segment_prefix(m_segment_prefix);
    cpu.m_effective_operand_size32 = m_o32;
    cpu.m_effective_address_size32 = m_a32;
    if (m_has_rm)
        m_modrm.resolve(cpu);
    (cpu.*m_impl)(*this);
}
