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
#include "machine.h"
#include "pic.h"
#include "pit.h"
#include "settings.h"
#include <unistd.h>

//#define DEBUG_PAGING
#define CRASH_ON_OPCODE_00_00
//#define CRASH_ON_EXECUTE_00000000
#define CRASH_ON_PE_JMP_00000000
#define CRASH_ON_VME
#define CRASH_ON_PVI
#define A20_ENABLED
#define DEBUG_PHYSICAL_OOB
//#define DEBUG_ON_UD0
//#define DEBUG_ON_UD1
//#define DEBUG_ON_UD2
#define MEMORY_DEBUGGING
//#define DEBUG_WARCRAFT2
//#define DEBUG_BOUND

#ifdef MEMORY_DEBUGGING
static bool should_log_all_memory_accesses(PhysicalAddress address)
{
    UNUSED_PARAM(address);
#    ifdef CT_DETERMINISTIC
    return true;
#    endif
    return false;
}

static bool should_log_memory_write(PhysicalAddress address)
{
    if (should_log_all_memory_accesses(address))
        return true;
    return false;
}

static bool should_log_memory_read(PhysicalAddress address)
{
    if (should_log_all_memory_accesses(address))
        return true;
    return false;
}
#endif

CPU* g_cpu = 0;

u32 CPU::read_register_for_address_size(int register_index)
{
    if (a32())
        return m_gpr[register_index].full_u32;
    return m_gpr[register_index].low_u16;
}

void CPU::write_register_for_address_size(int register_index, u32 data)
{
    if (a32())
        m_gpr[register_index].full_u32 = data;
    else
        m_gpr[register_index].low_u16 = data;
}

void CPU::step_register_for_address_size(int register_index, u32 step_size)
{
    if (a32())
        m_gpr[register_index].full_u32 += get_df() ? -step_size : step_size;
    else
        m_gpr[register_index].low_u16 += get_df() ? -step_size : step_size;
}

bool CPU::decrement_cx_for_address_size()
{
    if (a32()) {
        set_ecx(get_ecx() - 1);
        return get_ecx() == 0;
    }
    set_cx(get_cx() - 1);
    return get_cx() == 0;
}

template u8 CPU::read_register<u8>(int) const;
template u16 CPU::read_register<u16>(int) const;
template u32 CPU::read_register<u32>(int) const;
template void CPU::write_register<u8>(int, u8);
template void CPU::write_register<u16>(int, u16);
template void CPU::write_register<u32>(int, u32);

FLATTEN void CPU::decodeNext()
{
#ifdef CT_TRACE
    if (UNLIKELY(m_is_for_autotest))
        dump_trace();
#endif

#ifdef CRASH_ON_EXECUTE_00000000
    if (UNLIKELY(current_base_instruction_pointer() == 0 && (get_pe() || !get_base_cs()))) {
        dump_all();
        vlog(LogCPU, "It seems like we've jumped to 00000000 :(");
        ASSERT_NOT_REACHED();
    }
#endif

#ifdef CRASH_ON_VME
    if (UNLIKELY(get_vme()))
        ASSERT_NOT_REACHED();
#endif

#ifdef CRASH_ON_PVI
    if (UNLIKELY(get_pvi()))
        ASSERT_NOT_REACHED();
#endif

    auto insn = Instruction::from_stream(*this, m_operand_size32, m_address_size32);
    if (!insn.is_valid())
        throw InvalidOpcode();
    execute(insn);
}

FLATTEN void CPU::execute(Instruction& insn)
{
#ifdef CRASH_ON_OPCODE_00_00
    if (UNLIKELY(insn.op() == 0 && insn.rm() == 0)) {
        dump_trace();
        ASSERT_NOT_REACHED();
    }
#endif

#ifdef DISASSEMBLE_EVERYTHING
    if (options.disassemble_everything)
        vlog(LogCPU, "%s", qPrintable(insn.to_string(m_base_eip, x32())));
#endif
    insn.execute(*this);

    ++m_cycle;
}

void CPU::_RDTSC(Instruction&)
{
    if (get_tsd() && get_pe() && get_cpl() != 0) {
        throw GeneralProtectionFault(0, "RDTSC");
    }
    set_edx(m_cycle >> 32);
    set_eax(m_cycle);
}

void CPU::_WBINVD(Instruction&)
{
    if (get_pe() && get_cpl() != 0) {
        throw GeneralProtectionFault(0, "WBINVD");
    }
}

void CPU::_INVLPG(Instruction&)
{
    if (get_pe() && get_cpl() != 0) {
        throw GeneralProtectionFault(0, "INVLPG");
    }
}

void CPU::_VKILL(Instruction&)
{
    // FIXME: Maybe (0xf1) is a bad choice of opcode here, since that's also INT1 / ICEBP.
    if (!machine().is_for_autotest()) {
        throw InvalidOpcode("VKILL (0xf1) is an invalid opcode outside of auto-test mode!");
    }
    vlog(LogCPU, "0xF1: Secret shutdown command received!");
    //dump_all();
    hard_exit(0);
}

void CPU::set_memory_size_and_reallocate_if_needed(u32 size)
{
    if (m_memory_size == size)
        return;
    delete[] m_memory;
    m_memory_size = size;
    m_memory = new u8[m_memory_size];
    if (!m_memory) {
        vlog(LogInit, "Insufficient memory available.");
        hard_exit(1);
    }
    memset(m_memory, 0x0, m_memory_size);
}

CPU::CPU(Machine& m)
    : m_machine(m)
{
#ifdef SYMBOLIC_TRACING
    {
        QFile file("win311.sym");
        file.open(QIODevice::ReadOnly);
        static QRegExp whitespaceRegExp("\\s");
        while (!file.atEnd()) {
            auto line = QString::fromLocal8Bit(file.readLine());
            auto parts = line.split(whitespaceRegExp, QString::SkipEmptyParts);
            m_symbols.insert(parts[0].toUInt(nullptr, 16), parts.last());
            m_symbols_reverse.insert(parts.last(), parts[0].toUInt(nullptr, 16));
        }
    }
#endif
#ifdef VMM_TRACING
    QFile file("windows_vmm.txt");
    file.open(QIODevice::ReadOnly);
    while (!file.atEnd()) {
        auto line = QString::fromLocal8Bit(file.readLine());
        m_vmm_names.append(line.trimmed());
    }
#endif
    m_is_for_autotest = machine().is_for_autotest();

    build_opcode_tables_if_needed();

    ASSERT(!g_cpu);
    g_cpu = this;

    set_memory_size_and_reallocate_if_needed(8192 * 1024);

    memset(m_memory_providers, 0, sizeof(m_memory_providers));

    m_debugger = make<Debugger>(*this);

    m_control_register_map[0] = &m_cr0;
    m_control_register_map[1] = nullptr;
    m_control_register_map[2] = &m_cr2;
    m_control_register_map[3] = &m_cr3;
    m_control_register_map[4] = &m_cr4;
    m_control_register_map[5] = nullptr;
    m_control_register_map[6] = nullptr;
    m_control_register_map[7] = nullptr;

    m_debug_register_map[0] = &m_dr0;
    m_debug_register_map[1] = &m_dr1;
    m_debug_register_map[2] = &m_dr2;
    m_debug_register_map[3] = &m_dr3;
    m_debug_register_map[4] = &m_dr4;
    m_debug_register_map[5] = &m_dr5;
    m_debug_register_map[6] = &m_dr6;
    m_debug_register_map[7] = &m_dr7;

    m_byte_registers[RegisterAH] = &m_gpr[RegisterEAX].high_u8;
    m_byte_registers[RegisterBH] = &m_gpr[RegisterEBX].high_u8;
    m_byte_registers[RegisterCH] = &m_gpr[RegisterECX].high_u8;
    m_byte_registers[RegisterDH] = &m_gpr[RegisterEDX].high_u8;
    m_byte_registers[RegisterAL] = &m_gpr[RegisterEAX].low_u8;
    m_byte_registers[RegisterBL] = &m_gpr[RegisterEBX].low_u8;
    m_byte_registers[RegisterCL] = &m_gpr[RegisterECX].low_u8;
    m_byte_registers[RegisterDL] = &m_gpr[RegisterEDX].low_u8;

    m_segment_map[(int)SegmentRegisterIndex::CS] = &this->m_cs;
    m_segment_map[(int)SegmentRegisterIndex::DS] = &this->m_ds;
    m_segment_map[(int)SegmentRegisterIndex::ES] = &this->m_es;
    m_segment_map[(int)SegmentRegisterIndex::SS] = &this->m_ss;
    m_segment_map[(int)SegmentRegisterIndex::FS] = &this->m_fs;
    m_segment_map[(int)SegmentRegisterIndex::GS] = &this->m_gs;
    m_segment_map[6] = nullptr;
    m_segment_map[7] = nullptr;

    reset();
}

void CPU::reset()
{
    m_a20_enabled = false;
    m_next_instruction_is_uninterruptible = false;

    memset(&m_gpr, 0, sizeof(m_gpr));
    m_cr0 = 0;
    m_cr2 = 0;
    m_cr3 = 0;
    m_cr4 = 0;
    m_dr0 = 0;
    m_dr1 = 0;
    m_dr2 = 0;
    m_dr3 = 0;
    m_dr4 = 0;
    m_dr5 = 0;
    m_dr6 = 0;
    m_dr7 = 0;

    this->m_iopl = 0;
    this->m_vm = 0;
    this->m_vip = 0;
    this->m_vif = 0;
    this->m_nt = 0;
    this->m_rf = 0;
    this->m_ac = 0;
    this->m_id = 0;

    m_gdtr.clear();
    m_idtr.clear();
    m_ldtr.clear();

    this->m_tr.selector = 0;
    this->m_tr.limit = 0xffff;
    this->m_tr.base = LinearAddress();
    this->m_tr.is_32bit = false;

    memset(m_descriptor, 0, sizeof(m_descriptor));

    m_segment_prefix = SegmentRegisterIndex::None;

    set_cs(0);
    set_ds(0);
    set_es(0);
    set_ss(0);
    set_fs(0);
    set_gs(0);

    if (m_is_for_autotest)
        far_jump(LogicalAddress(machine().settings().entry_cs(), machine().settings().entry_ip()), JumpType::Internal);
    else
        far_jump(LogicalAddress(0xf000, 0x0000), JumpType::Internal);

    set_flags(0x0200);

    set_iopl(3);

    m_state = Alive;

    m_address_size32 = false;
    m_operand_size32 = false;
    m_effective_address_size32 = false;
    m_effective_operand_size32 = false;

    m_dirty_flags = 0;
    m_last_result = 0;
    m_last_op_size = ByteSize;

    m_cycle = 0;

    init_watches();

    recompute_main_loop_needs_slow_stuff();
}

CPU::~CPU()
{
    delete[] m_memory;
    m_memory = nullptr;
}

class InstructionExecutionContext {
public:
    InstructionExecutionContext(CPU& cpu)
        : m_cpu(cpu)
    {
        cpu.save_base_address();
    }
    ~InstructionExecutionContext() { m_cpu.clearPrefix(); }

private:
    CPU& m_cpu;
};

FLATTEN void CPU::execute_one_instruction()
{
    try {
        InstructionExecutionContext context(*this);
#ifdef SYMBOLIC_TRACING
        auto it = m_symbols.find(get_eip());
        if (it != m_symbols.end()) {
            vlog(LogCPU, "\033[34;1m%s\033[0m", qPrintable(*it));
        }
#endif
        decodeNext();
    } catch (Exception e) {
        if (options.log_exceptions)
            dump_disassembled(cached_descriptor(SegmentRegisterIndex::CS), m_base_eip, 3);
        raise_exception(e);
    } catch (HardwareInterruptDuringREP) {
        set_eip(current_base_instruction_pointer());
    }
}

void CPU::halted_loop()
{
    while (state() == CPU::Halted) {
#ifdef HAVE_USLEEP
        usleep(100);
#endif
        if (m_should_hard_reboot) {
            hard_reboot();
            return;
        }
        if (debugger().is_active()) {
            save_base_address();
            debugger().do_console();
        }
        if (PIC::has_pending_irq() && get_if())
            PIC::service_irq(*this);
    }
}

void CPU::queue_command(Command command)
{
    switch (command) {
    case EnterDebugger:
        m_debugger_request = PleaseEnterDebugger;
        break;
    case ExitDebugger:
        m_debugger_request = PleaseExitDebugger;
        break;
    case HardReboot:
        m_should_hard_reboot = true;
        break;
    }
    recompute_main_loop_needs_slow_stuff();
}

void CPU::hard_reboot()
{
    machine().reset_all_io_devices();
    reset();
    m_should_hard_reboot = false;
}

void CPU::make_next_instruction_uninterruptible()
{
    m_next_instruction_is_uninterruptible = true;
}

void CPU::recompute_main_loop_needs_slow_stuff()
{
    m_main_loop_needs_slow_stuff = m_debugger_request != NoDebuggerRequest || m_should_hard_reboot || options.trace || !m_breakpoints.empty() || debugger().is_active() || !m_watches.isEmpty();
}

NEVER_INLINE bool CPU::main_loop_slow_stuff()
{
    if (m_should_hard_reboot) {
        hard_reboot();
        return true;
    }

    if (!m_breakpoints.empty()) {
        for (auto& breakpoint : m_breakpoints) {
            if (get_cs() == breakpoint.selector() && get_eip() == breakpoint.offset()) {
                debugger().enter();
                break;
            }
        }
    }

    if (m_debugger_request == PleaseEnterDebugger) {
        debugger().enter();
        m_debugger_request = NoDebuggerRequest;
        recompute_main_loop_needs_slow_stuff();
    } else if (m_debugger_request == PleaseExitDebugger) {
        debugger().exit();
        m_debugger_request = NoDebuggerRequest;
        recompute_main_loop_needs_slow_stuff();
    }

    if (debugger().is_active()) {
        save_base_address();
        debugger().do_console();
    }

    if (options.trace)
        dump_trace();

    if (!m_watches.isEmpty())
        dump_watches();

    return true;
}

FLATTEN void CPU::main_loop()
{
    forever
    {
        if (UNLIKELY(m_main_loop_needs_slow_stuff)) {
            main_loop_slow_stuff();
        }

        execute_one_instruction();

        // FIXME: An obvious optimization here would be to dispatch next insn directly from whoever put us in this state.
        // Easy to implement: just call executeOneInstruction() in e.g "POP SS"
        // I'll do this once things feel more trustworthy in general.
        if (UNLIKELY(m_next_instruction_is_uninterruptible)) {
            m_next_instruction_is_uninterruptible = false;
            continue;
        }

        if (UNLIKELY(get_tf())) {
            // The Trap Flag is set, so we'll execute one instruction and
            // call ISR 1 as soon as it's finished.
            //
            // This is used by tools like DEBUG to implement step-by-step
            // execution :-)
            interrupt(1, InterruptSource::Internal);
        }

        if (PIC::has_pending_irq() && get_if())
            PIC::service_irq(*this);

#ifdef CT_DETERMINISTIC
        if (getIF() && ((cycle() + 1) % 100 == 0)) {
            machine().pit().raise_irq();
        }
#endif
    }
}

void CPU::jump_relative8(i8 displacement)
{
    m_eip += displacement;
}

void CPU::jump_relative16(i16 displacement)
{
    m_eip += displacement;
}

void CPU::jump_relative32(i32 displacement)
{
    m_eip += displacement;
}

void CPU::jump_absolute16(u16 address)
{
    m_eip = address;
}

void CPU::jump_absolute32(u32 address)
{
#ifdef CRASH_ON_PE_JMP_00000000
    if (get_pe() && !address) {
        vlog(LogCPU, "HMM! Jump to cs:00000000 in PE=1, source: %04x:%08x\n", get_base_cs(), get_base_eip());
        dump_all();
        ASSERT_NOT_REACHED();
    }
#endif
    //    vlog(LogCPU, "[PE=%u] Abs jump to %08X", get_pe(), address);
    m_eip = address;
}

static const char* to_string(JumpType type)
{
    switch (type) {
    case JumpType::CALL:
        return "CALL";
    case JumpType::RETF:
        return "RETF";
    case JumpType::IRET:
        return "IRET";
    case JumpType::INT:
        return "INT";
    case JumpType::JMP:
        return "JMP";
    case JumpType::Internal:
        return "Internal";
    default:
        ASSERT_NOT_REACHED();
        return nullptr;
    }
}

void CPU::real_mode_far_jump(LogicalAddress address, JumpType type)
{
    ASSERT(!get_pe() || get_vm());
    u16 selector = address.selector();
    u32 offset = address.offset();
    u16 original_cs = get_cs();
    u32 original_eip = get_eip();

#ifdef LOG_FAR_JUMPS
    vlog(LogCPU, "[PE=%u, VM=%u] %s from %04x:%08x to %04x:%08x", get_pe(), get_vm(), to_string(type), get_base_cs(), current_base_instruction_pointer(), selector, offset);
#endif

    set_cs(selector);
    set_eip(offset);

    if (type == JumpType::CALL) {
#ifdef DEBUG_JUMPS
        vlog(LogCPU, "Push %u-bit cs:eip %04x:%08x @stack{%04x:%08x}", o16() ? 16 : 32, original_cs, original_eip, get_ss(), get_esp());
#endif
        push_operand_sized_value(original_cs);
        push_operand_sized_value(original_eip);
    }
}

void CPU::far_jump(LogicalAddress address, JumpType type, Gate* gate)
{
    if (!get_pe() || get_vm()) {
        real_mode_far_jump(address, type);
    } else {
        protected_mode_far_jump(address, type, gate);
    }
}

void CPU::protected_mode_far_jump(LogicalAddress address, JumpType type, Gate* gate)
{
    ASSERT(get_pe());
    u16 selector = address.selector();
    u32 offset = address.offset();
    ValueSize push_size = o32() ? DWordSize : WordSize;

    if (gate) {
        // Coming through a gate; respect bit size of gate descriptor!
        push_size = gate->is_32bit() ? DWordSize : WordSize;
    }

    u16 original_ss = get_ss();
    u32 original_esp = get_esp();
    u16 original_cpl = get_cpl();
    u16 original_cs = get_cs();
    u32 original_eip = get_eip();

    u8 selectorRPL = selector & 3;

#ifdef LOG_FAR_JUMPS
    vlog(LogCPU, "[PE=%u, PG=%u] %s from %04x:%08x to %04x:%08x", get_pe(), get_pg(), to_string(type), get_base_cs(), current_base_instruction_pointer(), selector, offset);
#endif

    auto descriptor = get_descriptor(selector);

    if (descriptor.is_null()) {
        throw GeneralProtectionFault(0, QString("%1 to null selector").arg(to_string(type)));
    }

    if (descriptor.is_outside_table_limits())
        throw GeneralProtectionFault(selector & 0xfffc, QString("%1 to selector outside table limit").arg(to_string(type)));

    if (!descriptor.is_code() && !descriptor.is_call_gate() && !descriptor.is_task_gate() && !descriptor.is_tss())
        throw GeneralProtectionFault(selector & 0xfffc, QString("%1 to invalid descriptor type").arg(to_string(type)));

    if (descriptor.is_gate() && gate) {
        dump_descriptor(*gate);
        dump_descriptor(descriptor);
        throw GeneralProtectionFault(selector & 0xfffc, "Gate-to-gate jumps are not allowed");
    }

    if (descriptor.is_task_gate()) {
        // FIXME: Implement JMP/CALL thorough task gate.
        ASSERT_NOT_REACHED();
    }

    if (descriptor.is_call_gate()) {
        auto& gate = descriptor.as_gate();
#ifdef DEBUG_JUMPS
        vlog(LogCPU, "Gate (%s) to %04x:%08x (count=%u)", gate.type_name(), gate.selector(), gate.offset(), gate.parameter_count());
#endif
        if (gate.parameter_count() != 0) {
            // FIXME: Implement gate parameter counts.
            ASSERT_NOT_REACHED();
        }

        if (gate.dpl() < get_cpl())
            throw GeneralProtectionFault(selector & 0xfffc, QString("%1 to gate with DPL(%2) < CPL(%3)").arg(to_string(type)).arg(gate.dpl()).arg(get_cpl()));

        if (selectorRPL > gate.dpl())
            throw GeneralProtectionFault(selector & 0xfffc, QString("%1 to gate with RPL(%2) > DPL(%3)").arg(to_string(type)).arg(selectorRPL).arg(gate.dpl()));

        if (!gate.present()) {
            throw NotPresent(selector & 0xfffc, QString("Gate not present"));
        }

        // NOTE: We recurse here, jumping to the gate entry point.
        far_jump(gate.entry(), type, &gate);
        return;
    }

    if (descriptor.is_tss()) {
        auto& tss_descriptor = descriptor.as_tss_descriptor();
#ifdef DEBUG_JUMPS
        vlog(LogCPU, "CS is this:");
        dump_descriptor(cached_descriptor(SegmentRegisterIndex::CS));
        vlog(LogCPU, "%s to TSS descriptor (%s) -> %08x", to_string(type), tss_descriptor.type_name(), tss_descriptor.base());
#endif
        if (tss_descriptor.dpl() < get_cpl())
            throw GeneralProtectionFault(selector & 0xfffc, QString("%1 to TSS descriptor with DPL < CPL").arg(to_string(type)));
        if (tss_descriptor.dpl() < selectorRPL)
            throw GeneralProtectionFault(selector & 0xfffc, QString("%1 to TSS descriptor with DPL < RPL").arg(to_string(type)));
        if (!tss_descriptor.present())
            throw NotPresent(selector & 0xfffc, "TSS not present");
        task_switch(selector, tss_descriptor, type);
        return;
    }

    // Okay, so it's a code segment then.
    auto& code_segment = descriptor.as_code_segment_descriptor();

    if ((type == JumpType::CALL || type == JumpType::JMP) && !gate) {
        if (code_segment.conforming()) {
            if (code_segment.dpl() > get_cpl()) {
                throw GeneralProtectionFault(selector & 0xfffc, QString("%1 -> Code segment DPL(%2) > CPL(%3)").arg(to_string(type)).arg(code_segment.dpl()).arg(get_cpl()));
            }
        } else {
            if (selectorRPL > code_segment.dpl()) {
                throw GeneralProtectionFault(selector & 0xfffc, QString("%1 -> Code segment RPL(%2) > CPL(%3)").arg(to_string(type)).arg(selectorRPL).arg(code_segment.dpl()));
            }
            if (code_segment.dpl() != get_cpl()) {
                throw GeneralProtectionFault(selector & 0xfffc, QString("%1 -> Code segment DPL(%2) != CPL(%3)").arg(to_string(type)).arg(code_segment.dpl()).arg(get_cpl()));
            }
        }
    }

    if (gate && !gate->is_32bit()) {
        offset &= 0xffff;
    }

    // NOTE: A 32-bit jump into a 16-bit segment might have irrelevant higher bits set.
    // Mask them off to make sure we don't incorrectly fail limit checks.
    if (!code_segment.is_32bit()) {
        offset &= 0xffff;
    }

    if (!code_segment.present()) {
        throw NotPresent(selector & 0xfffc, QString("Code segment not present"));
    }

    if (offset > code_segment.effective_limit()) {
        vlog(LogCPU, "%s to eip(%08x) outside limit(%08x)", to_string(type), offset, code_segment.effective_limit());
        dump_descriptor(code_segment);
        throw GeneralProtectionFault(0, "Offset outside segment limit");
    }

    set_cs(selector);
    set_eip(offset);

    if (type == JumpType::CALL && gate) {
        if (descriptor.dpl() < original_cpl) {
#ifdef DEBUG_JUMPS
            vlog(LogCPU, "%s escalating privilege from ring%u to ring%u", to_string(type), original_cpl, descriptor.dpl(), descriptor);
#endif
            auto tss = current_tss();

            u16 new_ss = tss.get_ring_ss(descriptor.dpl());
            u32 new_esp = tss.get_ring_esp(descriptor.dpl());
            auto new_ss_descriptor = get_descriptor(new_ss);

            // FIXME: For JumpType::INT, exceptions related to newSS should contain the extra error code.

            if (new_ss_descriptor.is_null()) {
                throw InvalidTSS(new_ss & 0xfffc, "New ss is null");
            }

            if (new_ss_descriptor.is_outside_table_limits()) {
                throw InvalidTSS(new_ss & 0xfffc, "New ss outside table limits");
            }

            if (new_ss_descriptor.dpl() != descriptor.dpl()) {
                throw InvalidTSS(new_ss & 0xfffc, QString("New ss DPL(%1) != code segment DPL(%2)").arg(new_ss_descriptor.dpl()).arg(descriptor.dpl()));
            }

            if (!new_ss_descriptor.is_data() || !new_ss_descriptor.as_data_segment_descriptor().writable()) {
                throw InvalidTSS(new_ss & 0xfffc, "New ss not a writable data segment");
            }

            if (!new_ss_descriptor.present()) {
                throw StackFault(new_ss & 0xfffc, "New ss not present");
            }

            BEGIN_ASSERT_NO_EXCEPTIONS
            set_cpl(descriptor.dpl());
            set_ss(new_ss);
            set_esp(new_esp);

#ifdef DEBUG_JUMPS
            vlog(LogCPU, "%s to inner ring, ss:sp %04x:%04x -> %04x:%04x", to_string(type), original_ss, original_esp, get_ss(), get_sp());
            vlog(LogCPU, "Push %u-bit ss:sp %04x:%04x @stack{%04x:%08x}", push_size, original_ss, original_esp, get_ss(), get_esp());
#endif
            push_value_with_size(original_ss, push_size);
            push_value_with_size(original_esp, push_size);
            END_ASSERT_NO_EXCEPTIONS
        } else {
#ifdef DEBUG_JUMPS
            vlog(LogCPU, "%s same privilege from ring%u to ring%u", to_string(type), original_cpl, descriptor.dpl());
#endif
            set_cpl(original_cpl);
        }
    }

    if (type == JumpType::CALL) {
#ifdef DEBUG_JUMPS
        vlog(LogCPU, "Push %u-bit cs:ip %04x:%04x @stack{%04x:%08x}", push_size, original_cs, original_eip, get_ss(), get_esp());
#endif
        BEGIN_ASSERT_NO_EXCEPTIONS
        push_value_with_size(original_cs, push_size);
        push_value_with_size(original_eip, push_size);
        END_ASSERT_NO_EXCEPTIONS
    }

    if (!gate)
        set_cpl(original_cpl);
}

void CPU::clear_segment_register_after_return_if_needed(SegmentRegisterIndex segreg, JumpType type)
{
    if (read_segment_register(segreg) == 0)
        return;
    auto& cached = cached_descriptor(segreg);
    if (cached.is_null() || (cached.dpl() < get_cpl() && (cached.is_data() || cached.is_nonconforming_code()))) {
        vlog(LogCPU, "%s clearing %s(%04x) with DPL=%u (CPL now %u)", to_string(type), register_name(segreg), read_segment_register(segreg), cached.dpl(), get_cpl());
        write_segment_register(segreg, 0);
    }
}

void CPU::protected_far_return(u16 stack_adjustment)
{
    ASSERT(get_pe());
#ifdef DEBUG_JUMPS
    u16 originalSS = get_ss();
    u32 originalESP = get_esp();
    u16 originalCS = get_cs();
    u32 originalEIP = get_eip();
#endif

    TransactionalPopper popper(*this);

    u32 offset = popper.pop_operand_sized_value();
    u16 selector = popper.pop_operand_sized_value();
    u16 original_cpl = get_cpl();
    u8 selector_rpl = selector & 3;

    popper.adjust_stack_pointer(stack_adjustment);

#ifdef LOG_FAR_JUMPS
    vlog(LogCPU, "[PE=%u, PG=%u] %s from %04x:%08x to %04x:%08x", get_pe(), getPG(), "RETF", get_base_cs(), current_base_instruction_pointer(), selector, offset);
#endif

    auto descriptor = get_descriptor(selector);

    if (descriptor.is_null())
        throw GeneralProtectionFault(0, "RETF to null selector");

    if (descriptor.is_outside_table_limits())
        throw GeneralProtectionFault(selector & 0xfffc, "RETF to selector outside table limit");

    if (!descriptor.is_code()) {
        dump_descriptor(descriptor);
        throw GeneralProtectionFault(selector & 0xfffc, "Not a code segment");
    }

    if (selector_rpl < get_cpl())
        throw GeneralProtectionFault(selector & 0xfffc, QString("RETF with RPL(%1) < CPL(%2)").arg(selector_rpl).arg(get_cpl()));

    auto& codeSegment = descriptor.as_code_segment_descriptor();

    if (codeSegment.conforming() && codeSegment.dpl() > selector_rpl)
        throw GeneralProtectionFault(selector & 0xfffc, "RETF to conforming code segment with DPL > RPL");

    if (!codeSegment.conforming() && codeSegment.dpl() != selector_rpl)
        throw GeneralProtectionFault(selector & 0xfffc, "RETF to non-conforming code segment with DPL != RPL");

    if (!codeSegment.present())
        throw NotPresent(selector & 0xfffc, "Code segment not present");

    // NOTE: A 32-bit jump into a 16-bit segment might have irrelevant higher bits set.
    // Mask them off to make sure we don't incorrectly fail limit checks.
    if (!codeSegment.is_32bit()) {
        offset &= 0xffff;
    }

    if (offset > codeSegment.effective_limit()) {
        vlog(LogCPU, "RETF to eip(%08x) outside limit(%08x)", offset, codeSegment.effective_limit());
        dump_descriptor(codeSegment);
        throw GeneralProtectionFault(0, "Offset outside segment limit");
    }

    // FIXME: Validate SS before clobbering CS:EIP.
    set_cs(selector);
    set_eip(offset);

    if (selector_rpl > original_cpl) {
        BEGIN_ASSERT_NO_EXCEPTIONS
        u32 newESP = popper.pop_operand_sized_value();
        u16 newSS = popper.pop_operand_sized_value();
#ifdef DEBUG_JUMPS
        vlog(LogCPU, "Popped %u-bit ss:esp %04x:%08x @stack{%04x:%08x}", o16() ? 16 : 32, newSS, newESP, get_ss(), popper.adjusted_stack_pointer());
        vlog(LogCPU, "%s from ring%u to ring%u, ss:esp %04x:%08x -> %04x:%08x", "RETF", original_cpl, get_cpl(), originalSS, originalESP, newSS, newESP);
#endif

        set_ss(newSS);
        set_esp(newESP);

        clear_segment_register_after_return_if_needed(SegmentRegisterIndex::ES, JumpType::RETF);
        clear_segment_register_after_return_if_needed(SegmentRegisterIndex::FS, JumpType::RETF);
        clear_segment_register_after_return_if_needed(SegmentRegisterIndex::GS, JumpType::RETF);
        clear_segment_register_after_return_if_needed(SegmentRegisterIndex::DS, JumpType::RETF);
        END_ASSERT_NO_EXCEPTIONS
    } else {
        popper.commit();
    }

    if (get_cpl() != original_cpl)
        adjust_stack_pointer(stack_adjustment);
}

void CPU::real_mode_far_return(u16 stack_adjustment)
{
    u32 offset = pop_operand_sized_value();
    u16 selector = pop_operand_sized_value();
    set_cs(selector);
    set_eip(offset);
    adjust_stack_pointer(stack_adjustment);
}

void CPU::far_return(u16 stack_adjustment)
{
    if (!get_pe() || get_vm()) {
        real_mode_far_return(stack_adjustment);
        return;
    }

    protected_far_return(stack_adjustment);
}

void CPU::set_cpl(u8 cpl)
{
    if (get_pe() && !get_vm())
        m_cs = (m_cs & ~3) | cpl;
    cached_descriptor(SegmentRegisterIndex::CS).m_rpl = cpl;
}

void CPU::_NOP(Instruction&)
{
}

void CPU::_HLT(Instruction&)
{
    if (get_cpl() != 0) {
        throw GeneralProtectionFault(0, QString("HLT with CPL!=0(%1)").arg(get_cpl()));
    }

    set_state(CPU::Halted);

    if (!get_if()) {
        vlog(LogCPU, "Halted with IF=0");
    } else {
#ifdef VERBOSE_DEBUG
        vlog(LogCPU, "Halted");
#endif
    }

    halted_loop();
}

void CPU::_XLAT(Instruction&)
{
    set_al(read_memory8(current_segment(), read_register_for_address_size(RegisterBX) + get_al()));
}

void CPU::_XCHG_AX_reg16(Instruction& insn)
{
    auto tmp = insn.reg16();
    insn.reg16() = get_ax();
    set_ax(tmp);
}

void CPU::_XCHG_EAX_reg32(Instruction& insn)
{
    auto tmp = insn.reg32();
    insn.reg32() = get_eax();
    set_eax(tmp);
}

void CPU::_XCHG_reg8_RM8(Instruction& insn)
{
    auto tmp = insn.modrm().read8();
    insn.modrm().write8(insn.reg8());
    insn.reg8() = tmp;
}

void CPU::_XCHG_reg16_RM16(Instruction& insn)
{
    auto tmp = insn.modrm().read16();
    insn.modrm().write16(insn.reg16());
    insn.reg16() = tmp;
}

void CPU::_XCHG_reg32_RM32(Instruction& insn)
{
    auto tmp = insn.modrm().read32();
    insn.modrm().write32(insn.reg32());
    insn.reg32() = tmp;
}

template<typename T, class Accessor>
void CPU::doDEC(Accessor accessor)
{
    T value = accessor.get();
    set_of(value == (T)std::numeric_limits<typename std::make_signed<T>::type>::min());
    accessor.set(--value);
    adjust_flag(value, value + 1, 1);
    update_flags<T>(value);
}

template<typename T, class Accessor>
void CPU::doINC(Accessor accessor)
{
    T value = accessor.get();
    set_of(value == (T)std::numeric_limits<typename std::make_signed<T>::type>::max());
    accessor.set(++value);
    adjust_flag(value, value - 1, 1);
    update_flags<T>(value);
}

void CPU::_DEC_reg16(Instruction& insn)
{
    doDEC<u16>(RegisterAccessor<u16>(insn.reg16()));
}

void CPU::_DEC_reg32(Instruction& insn)
{
    doDEC<u32>(RegisterAccessor<u32>(insn.reg32()));
}

void CPU::_INC_reg16(Instruction& insn)
{
    doINC<u16>(RegisterAccessor<u16>(insn.reg16()));
}

void CPU::_INC_reg32(Instruction& insn)
{
    doINC<u32>(RegisterAccessor<u32>(insn.reg32()));
}

void CPU::_INC_RM16(Instruction& insn)
{
    doINC<u16>(insn.modrm().accessor16());
}

void CPU::_INC_RM32(Instruction& insn)
{
    doINC<u32>(insn.modrm().accessor32());
}

void CPU::_DEC_RM16(Instruction& insn)
{
    doDEC<u16>(insn.modrm().accessor16());
}

void CPU::_DEC_RM32(Instruction& insn)
{
    doDEC<u32>(insn.modrm().accessor32());
}

void CPU::_INC_RM8(Instruction& insn)
{
    doINC<u8>(insn.modrm().accessor8());
}

void CPU::_DEC_RM8(Instruction& insn)
{
    doDEC<u8>(insn.modrm().accessor8());
}

template<typename T>
void CPU::doLxS(Instruction& insn, SegmentRegisterIndex segreg)
{
    if (insn.modrm().is_register()) {
        throw InvalidOpcode("LxS with register operand");
    }
    auto address = read_logical_address<T>(insn.modrm().segment(), insn.modrm().offset());
    write_segment_register(segreg, address.selector());
    insn.reg<T>() = address.offset();
}

void CPU::_LDS_reg16_mem16(Instruction& insn)
{
    doLxS<u16>(insn, SegmentRegisterIndex::DS);
}

void CPU::_LDS_reg32_mem32(Instruction& insn)
{
    doLxS<u32>(insn, SegmentRegisterIndex::DS);
}

void CPU::_LES_reg16_mem16(Instruction& insn)
{
    doLxS<u16>(insn, SegmentRegisterIndex::ES);
}

void CPU::_LES_reg32_mem32(Instruction& insn)
{
    doLxS<u32>(insn, SegmentRegisterIndex::ES);
}

void CPU::_LFS_reg16_mem16(Instruction& insn)
{
    doLxS<u16>(insn, SegmentRegisterIndex::FS);
}

void CPU::_LFS_reg32_mem32(Instruction& insn)
{
    doLxS<u32>(insn, SegmentRegisterIndex::FS);
}

void CPU::_LSS_reg16_mem16(Instruction& insn)
{
    doLxS<u16>(insn, SegmentRegisterIndex::SS);
}

void CPU::_LSS_reg32_mem32(Instruction& insn)
{
    doLxS<u32>(insn, SegmentRegisterIndex::SS);
}

void CPU::_LGS_reg16_mem16(Instruction& insn)
{
    doLxS<u16>(insn, SegmentRegisterIndex::GS);
}

void CPU::_LGS_reg32_mem32(Instruction& insn)
{
    doLxS<u32>(insn, SegmentRegisterIndex::GS);
}

void CPU::_LEA_reg32_mem32(Instruction& insn)
{
    if (insn.modrm().is_register()) {
        throw InvalidOpcode("LEA_reg32_mem32 with register source");
    }
    insn.reg32() = insn.modrm().offset();
}

void CPU::_LEA_reg16_mem16(Instruction& insn)
{
    if (insn.modrm().is_register()) {
        throw InvalidOpcode("LEA_reg16_mem16 with register source");
    }
    insn.reg16() = insn.modrm().offset();
}

static const char* to_string(CPU::MemoryAccessType type)
{
    switch (type) {
    case CPU::MemoryAccessType::Read:
        return "Read";
    case CPU::MemoryAccessType::Write:
        return "Write";
    case CPU::MemoryAccessType::Execute:
        return "Execute";
    case CPU::MemoryAccessType::InternalPointer:
        return "InternalPointer";
    default:
        return "(wat)";
    }
}

PhysicalAddress CPU::translate_address(LinearAddress linear_address, MemoryAccessType access_type, u8 effective_cpl)
{
    if (!get_pe() || !get_pg())
        return PhysicalAddress(linear_address.get());
    return translate_address_slow_case(linear_address, access_type, effective_cpl);
}

static u16 makePFErrorCode(PageFaultFlags::Flags flags, CPU::MemoryAccessType access_type, bool user_mode)
{
    return flags
        | (access_type == CPU::MemoryAccessType::Write ? PageFaultFlags::Write : PageFaultFlags::Read)
        | (user_mode ? PageFaultFlags::UserMode : PageFaultFlags::SupervisorMode)
        | (access_type == CPU::MemoryAccessType::Execute ? PageFaultFlags::InstructionFetch : 0);
}

Exception CPU::PageFault(LinearAddress linear_address, PageFaultFlags::Flags flags, CPU::MemoryAccessType access_type, bool user_mode, const char* fault_table, u32 pde, u32 pte)
{
    u16 error = makePFErrorCode(flags, access_type, user_mode);
    if (options.log_exceptions) {
        vlog(LogCPU, "Exception: #PF(%04x) %s in %s for %s %s @%08x, PDBR=%08x, PDE=%08x, PTE=%08x",
            error,
            (flags & PageFaultFlags::ProtectionViolation) ? "PV" : "NP",
            fault_table,
            user_mode ? "User" : "Supervisor",
            to_string(access_type),
            linear_address.get(),
            get_cr3(),
            pde,
            pte);
    }
    m_cr2 = linear_address.get();
    if (options.crash_on_page_fault) {
        dump_all();
        vlog(LogAlert, "CRASH ON #PF");
        ASSERT_NOT_REACHED();
    }
#ifdef DEBUG_WARCRAFT2
    if (get_eip() == 0x100c2f7c) {
        vlog(LogAlert, "CRASH ON specific #PF");
        ASSERT_NOT_REACHED();
    }
#endif
    return Exception(0xe, error, linear_address.get(), "Page fault");
}

PhysicalAddress CPU::translate_address_slow_case(LinearAddress linear_address, MemoryAccessType access_type, u8 effective_cpl)
{
    ASSERT(get_cr3() < m_memory_size);

    u32 dir = (linear_address.get() >> 22) & 0x3FF;
    u32 page = (linear_address.get() >> 12) & 0x3FF;
    u32 offset = linear_address.get() & 0xFFF;

    ASSERT(!(get_cr3() & 0x03ff));

    PhysicalAddress pde_address(get_cr3() + dir * sizeof(u32));
    u32 page_directory_entry = read_physical_memory<u32>(pde_address);
    PhysicalAddress pte_address((page_directory_entry & 0xfffff000) + page * sizeof(u32));
    u32 page_table_entry = read_physical_memory<u32>(pte_address);

    bool user_mode;
    if (effective_cpl == 0xff)
        user_mode = get_cpl() == 3;
    else
        user_mode = effective_cpl == 3;

    if (!(page_directory_entry & PageTableEntryFlags::Present)) {
        throw PageFault(linear_address, PageFaultFlags::NotPresent, access_type, user_mode, "PDE", page_directory_entry);
    }

    if (!(page_table_entry & PageTableEntryFlags::Present)) {
        throw PageFault(linear_address, PageFaultFlags::NotPresent, access_type, user_mode, "PTE", page_directory_entry, page_table_entry);
    }

    if (user_mode) {
        if (!(page_directory_entry & PageTableEntryFlags::UserSupervisor)) {
            throw PageFault(linear_address, PageFaultFlags::ProtectionViolation, access_type, user_mode, "PDE", page_directory_entry);
        }
        if (!(page_table_entry & PageTableEntryFlags::UserSupervisor)) {
            throw PageFault(linear_address, PageFaultFlags::ProtectionViolation, access_type, user_mode, "PTE", page_directory_entry, page_table_entry);
        }
    }

    if ((user_mode || get_cr0() & CR0::WP) && access_type == MemoryAccessType::Write) {
        if (!(page_directory_entry & PageTableEntryFlags::ReadWrite)) {
            throw PageFault(linear_address, PageFaultFlags::ProtectionViolation, access_type, user_mode, "PDE", page_directory_entry);
        }
        if (!(page_table_entry & PageTableEntryFlags::ReadWrite)) {
            throw PageFault(linear_address, PageFaultFlags::ProtectionViolation, access_type, user_mode, "PTE", page_directory_entry, page_table_entry);
        }
    }

    if (access_type == MemoryAccessType::Write)
        page_table_entry |= PageTableEntryFlags::Dirty;

    page_directory_entry |= PageTableEntryFlags::Accessed;
    page_table_entry |= PageTableEntryFlags::Accessed;

    write_physical_memory(pde_address, page_directory_entry);
    write_physical_memory(pte_address, page_table_entry);

    PhysicalAddress physical_address((page_table_entry & 0xfffff000) | offset);
#ifdef DEBUG_PAGING
    if (options.log_page_translations)
        vlog(LogCPU, "PG=1 Translating %08x {dir=%03x, page=%03x, offset=%03x} => %08x [%08x + %08x] <PTE @ %08x>", linear_address.get(), dir, page, offset, physical_address.get(), page_directory_entry, page_table_entry, pte_address);
#endif
    return physical_address;
}

void CPU::snoop(LinearAddress linear_address, MemoryAccessType access_type)
{
    translate_address(linear_address, access_type);
}

void CPU::snoop(SegmentRegisterIndex segreg, u32 offset, MemoryAccessType access_type)
{
    // FIXME: Support multi-byte snoops.
    if (get_pe() && !get_vm())
        validate_address<u8>(segreg, offset, access_type);
    auto linear_address = cached_descriptor(segreg).linear_address(offset);
    snoop(linear_address, access_type);
}

template<typename T>
ALWAYS_INLINE void CPU::validate_address(const SegmentDescriptor& descriptor, u32 offset, MemoryAccessType access_type)
{
    if (!get_vm()) {
        if (access_type != MemoryAccessType::Execute) {
            if (descriptor.is_null()) {
                vlog(LogAlert, "NULL! %s offset %08X into null selector (selector index: %04X)",
                    to_string(access_type),
                    offset,
                    descriptor.index());
                if (descriptor.m_loaded_in_ss)
                    throw StackFault(0, "Access through null selector");
                else
                    throw GeneralProtectionFault(0, "Access through null selector");
            }
        }

        switch (access_type) {
        case MemoryAccessType::Read:
            if (descriptor.is_code() && !descriptor.as_code_segment_descriptor().readable()) {
                throw GeneralProtectionFault(0, "Attempt to read from non-readable code segment");
            }
            break;
        case MemoryAccessType::Write:
            if (!descriptor.is_data()) {
                if (descriptor.m_loaded_in_ss)
                    throw StackFault(0, "Attempt to write to non-data segment");
                else
                    throw GeneralProtectionFault(0, "Attempt to write to non-data segment");
            }
            if (!descriptor.as_data_segment_descriptor().writable()) {
                if (descriptor.m_loaded_in_ss)
                    throw StackFault(0, "Attempt to write to non-writable data segment");
                else
                    throw GeneralProtectionFault(0, "Attempt to write to non-writable data segment");
            }
            break;
        case MemoryAccessType::Execute:
            // CS should never point to a non-code segment.
            ASSERT(descriptor.is_code());
            break;
        default:
            break;
        }
    }

#if 0
    // FIXME: Is this appropriate somehow? Need to figure it out. The code below as-is breaks IRET.
    if (get_cpl() > descriptor.dpl()) {
        throw GeneralProtectionFault(0, QString("Insufficient privilege for access (CPL=%1, DPL=%2)").arg(get_cpl()).arg(descriptor.dpl()));
    }
#endif

    if (UNLIKELY((offset + (sizeof(T) - 1)) > descriptor.effective_limit())) {
        vlog(LogAlert, "%zu-bit %s offset %08X outside limit (selector index: %04X, effective limit: %08X [%08X x %s])",
            sizeof(T) * 8,
            to_string(access_type),
            offset,
            descriptor.index(),
            descriptor.effective_limit(),
            descriptor.limit(),
            descriptor.granularity() ? "4K" : "1b");
        //ASSERT_NOT_REACHED();
        dump_descriptor(descriptor);
        //dump_all();
        //debugger().enter();
        if (descriptor.m_loaded_in_ss)
            throw StackFault(0, "Access outside segment limit");
        else
            throw GeneralProtectionFault(0, "Access outside segment limit");
    }
}

template<typename T>
ALWAYS_INLINE void CPU::validate_address(SegmentRegisterIndex segreg, u32 offset, MemoryAccessType access_type)
{
    validate_address<T>(cached_descriptor(segreg), offset, access_type);
}

template<typename T>
ALWAYS_INLINE bool CPU::validate_physical_address(PhysicalAddress physical_address, MemoryAccessType access_type)
{
    UNUSED_PARAM(access_type);
    if (physical_address.get() < m_memory_size)
        return true;
    return false;
}

template<typename T>
T CPU::read_physical_memory(PhysicalAddress physical_address)
{
    if (!validate_physical_address<T>(physical_address, MemoryAccessType::Read)) {
        vlog(LogCPU, "Read outside physical memory: %08x", physical_address.get());
#ifdef DEBUG_PHYSICAL_OOB
        debugger().enter();
#endif
        return 0;
    }
    if (auto* provider = memory_provider_for_address(physical_address)) {
        if (auto* direct_read_access_pointer = provider->pointer_for_direct_read_access()) {
            return *reinterpret_cast<const T*>(&direct_read_access_pointer[physical_address.get() - provider->base_address().get()]);
        }
        return provider->read<T>(physical_address.get());
    }
    return *reinterpret_cast<T*>(&m_memory[physical_address.get()]);
}

template u8 CPU::read_physical_memory<u8>(PhysicalAddress);
template u16 CPU::read_physical_memory<u16>(PhysicalAddress);
template u32 CPU::read_physical_memory<u32>(PhysicalAddress);

template<typename T>
void CPU::write_physical_memory(PhysicalAddress physical_address, T data)
{
    if (!validate_physical_address<T>(physical_address, MemoryAccessType::Write)) {
        vlog(LogCPU, "Write outside physical memory: %08x", physical_address.get());
#ifdef DEBUG_PHYSICAL_OOB
        debugger().enter();
#endif
        return;
    }
    if (auto* provider = memory_provider_for_address(physical_address)) {
        provider->write<T>(physical_address.get(), data);
    } else {
        *reinterpret_cast<T*>(&m_memory[physical_address.get()]) = data;
    }
}

template void CPU::write_physical_memory<u8>(PhysicalAddress, u8);
template void CPU::write_physical_memory<u16>(PhysicalAddress, u16);
template void CPU::write_physical_memory<u32>(PhysicalAddress, u32);

template<typename T>
ALWAYS_INLINE T CPU::read_memory(LinearAddress linear_address, MemoryAccessType access_type, u8 effective_cpl)
{
    // FIXME: This needs to be optimized.
    if constexpr (sizeof(T) == 4) {
        if (get_pg() && (linear_address.get() & 0xfffff000) != (((linear_address.get() + (sizeof(T) - 1)) & 0xfffff000))) {
            u8 b1 = read_memory<u8>(linear_address.offset(0), access_type, effective_cpl);
            u8 b2 = read_memory<u8>(linear_address.offset(1), access_type, effective_cpl);
            u8 b3 = read_memory<u8>(linear_address.offset(2), access_type, effective_cpl);
            u8 b4 = read_memory<u8>(linear_address.offset(3), access_type, effective_cpl);
            return weld<u32>(weld<u16>(b4, b3), weld<u16>(b2, b1));
        }
    } else if constexpr (sizeof(T) == 2) {
        if (get_pg() && (linear_address.get() & 0xfffff000) != (((linear_address.get() + (sizeof(T) - 1)) & 0xfffff000))) {
            u8 b1 = read_memory<u8>(linear_address.offset(0), access_type, effective_cpl);
            u8 b2 = read_memory<u8>(linear_address.offset(1), access_type, effective_cpl);
            return weld<u16>(b2, b1);
        }
    }

    auto physical_address = translate_address(linear_address, access_type, effective_cpl);
#ifdef A20_ENABLED
    physical_address.mask(a20_mask());
#endif
    T value = read_physical_memory<T>(physical_address);
#ifdef MEMORY_DEBUGGING
    if (options.memdebug || should_log_memory_read(physical_address)) {
        if (options.novlog)
            printf("%04X:%08X: %zu-bit read [A20=%s] 0x%08X, value: %08X\n", get_base_cs(), current_base_instruction_pointer(), sizeof(T) * 8, is_a20_enabled() ? "on" : "off", physical_address.get(), value);
        else
            vlog(LogCPU, "%zu-bit read [A20=%s] 0x%08X, value: %08X", sizeof(T) * 8, is_a20_enabled() ? "on" : "off", physical_address.get(), value);
    }
#endif
    return value;
}

template<typename T>
ALWAYS_INLINE T CPU::read_memory(const SegmentDescriptor& descriptor, u32 offset, MemoryAccessType access_type)
{
    auto linear_address = descriptor.linear_address(offset);
    if (get_pe() && !get_vm())
        validate_address<T>(descriptor, offset, access_type);
    return read_memory<T>(linear_address, access_type);
}

template<typename T>
ALWAYS_INLINE T CPU::read_memory(SegmentRegisterIndex segreg, u32 offset, MemoryAccessType access_type)
{
    return read_memory<T>(cached_descriptor(segreg), offset, access_type);
}

template<typename T>
ALWAYS_INLINE T CPU::read_memory_metal(LinearAddress laddr)
{
    return read_memory<T>(laddr, MemoryAccessType::Read, 0);
}

template<typename T>
ALWAYS_INLINE void CPU::write_memory_metal(LinearAddress laddr, T value)
{
    return write_memory<T>(laddr, value, 0);
}

template u8 CPU::read_memory<u8>(SegmentRegisterIndex, u32, MemoryAccessType);
template u16 CPU::read_memory<u16>(SegmentRegisterIndex, u32, MemoryAccessType);
template u32 CPU::read_memory<u32>(SegmentRegisterIndex, u32, MemoryAccessType);

template void CPU::write_memory<u8>(SegmentRegisterIndex, u32, u8);
template void CPU::write_memory<u16>(SegmentRegisterIndex, u32, u16);
template void CPU::write_memory<u32>(SegmentRegisterIndex, u32, u32);

template void CPU::write_memory<u8>(LinearAddress, u8, u8);

template u16 CPU::read_memory_metal<u16>(LinearAddress);
template u32 CPU::read_memory_metal<u32>(LinearAddress);

u8 CPU::read_memory8(LinearAddress address) { return read_memory<u8>(address); }
u16 CPU::read_memory16(LinearAddress address) { return read_memory<u16>(address); }
u32 CPU::read_memory32(LinearAddress address) { return read_memory<u32>(address); }
u16 CPU::read_memory_metal16(LinearAddress address) { return read_memory_metal<u16>(address); }
u32 CPU::read_memory_metal32(LinearAddress address) { return read_memory_metal<u32>(address); }
u8 CPU::read_memory8(SegmentRegisterIndex segment, u32 offset) { return read_memory<u8>(segment, offset); }
u16 CPU::read_memory16(SegmentRegisterIndex segment, u32 offset) { return read_memory<u16>(segment, offset); }
u32 CPU::read_memory32(SegmentRegisterIndex segment, u32 offset) { return read_memory<u32>(segment, offset); }

template<typename T>
LogicalAddress CPU::read_logical_address(SegmentRegisterIndex segreg, u32 offset)
{
    LogicalAddress address;
    address.set_offset(read_memory<T>(segreg, offset));
    address.set_selector(read_memory16(segreg, offset + sizeof(T)));
    return address;
}

template LogicalAddress CPU::read_logical_address<u16>(SegmentRegisterIndex, u32);
template LogicalAddress CPU::read_logical_address<u32>(SegmentRegisterIndex, u32);

template<typename T>
void CPU::write_memory(LinearAddress linear_address, T value, u8 effectiveCPL)
{
    // FIXME: This needs to be optimized.
    if constexpr (sizeof(T) == 4) {
        if (get_pg() && (linear_address.get() & 0xfffff000) != (((linear_address.get() + (sizeof(T) - 1)) & 0xfffff000))) {
            write_memory<u8>(linear_address.offset(0), value & 0xff, effectiveCPL);
            write_memory<u8>(linear_address.offset(1), (value >> 8) & 0xff, effectiveCPL);
            write_memory<u8>(linear_address.offset(2), (value >> 16) & 0xff, effectiveCPL);
            write_memory<u8>(linear_address.offset(3), (value >> 24) & 0xff, effectiveCPL);
            return;
        }
    } else if constexpr (sizeof(T) == 2) {
        if (get_pg() && (linear_address.get() & 0xfffff000) != (((linear_address.get() + (sizeof(T) - 1)) & 0xfffff000))) {
            write_memory<u8>(linear_address.offset(0), value & 0xff, effectiveCPL);
            write_memory<u8>(linear_address.offset(1), (value >> 8) & 0xff, effectiveCPL);
            return;
        }
    }

    auto physical_address = translate_address(linear_address, MemoryAccessType::Write, effectiveCPL);
#ifdef A20_ENABLED
    physical_address.mask(a20_mask());
#endif
#ifdef MEMORY_DEBUGGING
    if (options.memdebug || should_log_memory_write(physical_address)) {
        if (options.novlog)
            printf("%04X:%08X: %zu-bit write [A20=%s] 0x%08X, value: %08X\n", get_base_cs(), current_base_instruction_pointer(), sizeof(T) * 8, is_a20_enabled() ? "on" : "off", physical_address.get(), value);
        else
            vlog(LogCPU, "%zu-bit write [A20=%s] 0x%08X, value: %08X", sizeof(T) * 8, is_a20_enabled() ? "on" : "off", physical_address.get(), value);
    }
#endif
    write_physical_memory(physical_address, value);
}

template<typename T>
void CPU::write_memory(const SegmentDescriptor& descriptor, u32 offset, T value)
{
    auto linear_address = descriptor.linear_address(offset);
    if (get_pe() && !get_vm())
        validate_address<T>(descriptor, offset, MemoryAccessType::Write);
    write_memory(linear_address, value);
}

template<typename T>
void CPU::write_memory(SegmentRegisterIndex segreg, u32 offset, T value)
{
    return write_memory<T>(cached_descriptor(segreg), offset, value);
}

void CPU::write_memory8(LinearAddress address, u8 value) { write_memory(address, value); }
void CPU::write_memory16(LinearAddress address, u16 value) { write_memory(address, value); }
void CPU::write_memory32(LinearAddress address, u32 value) { write_memory(address, value); }
void CPU::write_memory_metal16(LinearAddress address, u16 value) { write_memory_metal(address, value); }
void CPU::write_memory_metal32(LinearAddress address, u32 value) { write_memory_metal(address, value); }
void CPU::write_memory8(SegmentRegisterIndex segment, u32 offset, u8 value) { write_memory(segment, offset, value); }
void CPU::write_memory16(SegmentRegisterIndex segment, u32 offset, u16 value) { write_memory(segment, offset, value); }
void CPU::write_memory32(SegmentRegisterIndex segment, u32 offset, u32 value) { write_memory(segment, offset, value); }

void CPU::update_default_sizes()
{
#ifdef VERBOSE_DEBUG
    bool oldO32 = m_operand_size32;
    bool oldA32 = m_address_size32;
#endif

    auto& cs_descriptor = cached_descriptor(SegmentRegisterIndex::CS);
    m_address_size32 = cs_descriptor.d();
    m_operand_size32 = cs_descriptor.d();

#ifdef VERBOSE_DEBUG
    if (oldO32 != m_operandSize32 || oldA32 != m_addressSize32) {
        vlog(LogCPU, "updateDefaultSizes PE=%u X:%u O:%u A:%u (newCS: %04X)", get_pe(), x16() ? 16 : 32, o16() ? 16 : 32, a16() ? 16 : 32, get_cs());
        dump_descriptor(cs_descriptor);
    }
#endif
}

void CPU::update_stack_size()
{
#ifdef VERBOSE_DEBUG
    bool old_s32 = m_stack_size32;
#endif

    auto& ssDescriptor = cached_descriptor(SegmentRegisterIndex::SS);
    m_stackSize32 = ssDescriptor.d();

#ifdef VERBOSE_DEBUG
    if (old_s32 != m_stack_size32) {
        vlog(LogCPU, "update_stack_size PE=%u S:%u (newSS: %04x)", get_pe(), s16() ? 16 : 32, get_ss());
        dump_descriptor(ssDescriptor);
    }
#endif
}

void CPU::update_code_segment_cache()
{
    // FIXME: We need some kind of fast pointer for fetching from CS:EIP.
}

void CPU::set_cs(u16 value)
{
    write_segment_register(SegmentRegisterIndex::CS, value);
}

void CPU::set_ds(u16 value)
{
    write_segment_register(SegmentRegisterIndex::DS, value);
}

void CPU::set_es(u16 value)
{
    write_segment_register(SegmentRegisterIndex::ES, value);
}

void CPU::set_ss(u16 value)
{
    write_segment_register(SegmentRegisterIndex::SS, value);
}

void CPU::set_fs(u16 value)
{
    write_segment_register(SegmentRegisterIndex::FS, value);
}

void CPU::set_gs(u16 value)
{
    write_segment_register(SegmentRegisterIndex::GS, value);
}

const u8* CPU::pointer_to_physical_memory(PhysicalAddress physical_address)
{
    if (!validate_physical_address<u8>(physical_address, MemoryAccessType::InternalPointer))
        return nullptr;
    if (auto* provider = memory_provider_for_address(physical_address))
        return provider->memory_pointer(physical_address.get());
    return &m_memory[physical_address.get()];
}

const u8* CPU::memory_pointer(SegmentRegisterIndex segreg, u32 offset)
{
    return memory_pointer(cached_descriptor(segreg), offset);
}

const u8* CPU::memory_pointer(const SegmentDescriptor& descriptor, u32 offset)
{
    auto linear_address = descriptor.linear_address(offset);
    if (get_pe() && !get_vm())
        validate_address<u8>(descriptor, offset, MemoryAccessType::InternalPointer);
    return memory_pointer(linear_address);
}

const u8* CPU::memory_pointer(LogicalAddress address)
{
    return memory_pointer(get_segment_descriptor(address.selector()), address.offset());
}

const u8* CPU::memory_pointer(LinearAddress linear_address)
{
    auto physical_address = translate_address(linear_address, MemoryAccessType::InternalPointer);
#ifdef A20_ENABLED
    physical_address.mask(a20_mask());
#endif
    return pointer_to_physical_memory(physical_address);
}

template<typename T>
ALWAYS_INLINE T CPU::read_instruction_stream()
{
    T data = read_memory<T>(SegmentRegisterIndex::CS, current_instruction_pointer(), MemoryAccessType::Execute);
    adjust_instruction_pointer(sizeof(T));
    return data;
}

u8 CPU::read_instruction8()
{
    return read_instruction_stream<u8>();
}

u16 CPU::read_instruction16()
{
    return read_instruction_stream<u16>();
}

u32 CPU::read_instruction32()
{
    return read_instruction_stream<u32>();
}

void CPU::_CPUID(Instruction&)
{
    if (get_eax() == 0) {
        set_eax(1);
        set_ebx(0x706d6f43);
        set_edx(0x6f727475);
        set_ecx(0x3638586e);
        return;
    }

    if (get_eax() == 1) {
        u32 stepping = 0;
        u32 model = 1;
        u32 family = 3;
        u32 type = 0;
        set_eax(stepping | (model << 4) | (family << 8) | (type << 12));
        set_ebx(0);
        set_edx((1 << 4) | (1 << 15)); // RDTSC + CMOV
        set_ecx(0);
        return;
    }

    if (get_eax() == 0x80000000) {
        set_eax(0x80000004);
        return;
    }

    if (get_eax() == 0x80000001) {
        set_eax(0);
        set_ebx(0);
        set_ecx(0);
        set_edx(0);
    }

    if (get_eax() == 0x80000002) {
        set_eax(0x61632049);
        set_ebx(0x2074276e);
        set_ecx(0x696c6562);
        set_edx(0x20657665);
        return;
    }

    if (get_eax() == 0x80000003) {
        set_eax(0x73277469);
        set_ebx(0x746f6e20);
        set_ecx(0x746e4920);
        set_edx(0x00216c65);
        return;
    }

    if (get_eax() == 0x80000004) {
        set_eax(0);
        set_ebx(0);
        set_ecx(0);
        set_edx(0);
        return;
    }
}

void CPU::init_watches()
{
}

void CPU::register_memory_provider(MemoryProvider& provider)
{
    if ((provider.base_address().get() + provider.size()) > 1048576) {
        vlog(LogConfig, "Can't register mapper with length %u @ %08x", provider.size(), provider.base_address().get());
        ASSERT_NOT_REACHED();
    }

    for (unsigned i = provider.base_address().get() / memory_provider_block_size; i < (provider.base_address().get() + provider.size()) / memory_provider_block_size; ++i) {
        vlog(LogConfig, "Register memory provider %p as mapper %u", &provider, i);
        m_memory_providers[i] = &provider;
    }
}

ALWAYS_INLINE MemoryProvider* CPU::memory_provider_for_address(PhysicalAddress address)
{
    if (address.get() >= 1048576)
        return nullptr;
    return m_memory_providers[address.get() / memory_provider_block_size];
}

template<typename T>
void CPU::doBOUND(Instruction& insn)
{
    if (insn.modrm().is_register()) {
        throw InvalidOpcode("BOUND with register operand");
    }
    T array_index = insn.reg32();
    T lower_bound = read_memory<T>(insn.modrm().segment(), insn.modrm().offset());
    T upper_bound = read_memory<T>(insn.modrm().segment(), insn.modrm().offset() + sizeof(T));
    bool within_bounds = array_index >= lower_bound && array_index <= upper_bound;
#ifdef DEBUG_BOUND
    vlog(LogCPU, "BOUND<%u> checking if %d is within [%d, %d]: %s",
        sizeof(T) * 8,
        array_index,
        lower_bound,
        upper_bound,
        within_bounds ? "yes" : "no");
#endif
    if (!within_bounds)
        throw BoundRangeExceeded(QString("%1 not within [%2, %3]").arg(array_index).arg(lower_bound).arg(upper_bound));
}

void CPU::_BOUND(Instruction& insn)
{
    if (o16())
        doBOUND<i16>(insn);
    else
        doBOUND<i32>(insn);
}

void CPU::_UD0(Instruction&)
{
    vlog(LogCPU, "UD0");
#ifdef DEBUG_ON_UD0
    debugger().enter();
#else
    throw InvalidOpcode("UD0");
#endif
}

void CPU::_UD1(Instruction&)
{
    vlog(LogCPU, "UD1");
#ifdef DEBUG_ON_UD1
    debugger().enter();
#else
    throw InvalidOpcode("UD1");
#endif
}

void CPU::_UD2(Instruction&)
{
    vlog(LogCPU, "UD2");
#ifdef DEBUG_ON_UD2
    debugger().enter();
#else
    throw InvalidOpcode("UD2");
#endif
}

void CPU::_BSWAP_reg32(Instruction& insn)
{
    insn.reg32() = __builtin_bswap32(insn.reg32());
}
