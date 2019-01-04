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
#include "machine.h"
#include "Common.h"
#include "debug.h"
#include "debugger.h"
#include "pic.h"
#include "settings.h"
#include <unistd.h>
#include "pit.h"
#include "Tasking.h"

#define CRASH_ON_OPCODE_00_00
//#define CRASH_ON_EXECUTE_00000000
#define CRASH_ON_PE_JMP_00000000
#define CRASH_ON_VME
#define CRASH_ON_PVI
#define A20_ENABLED
#define DEBUG_PHYSICAL_OOB
//#define DEBUG_ON_UD0
#define DEBUG_ON_UD1
//#define DEBUG_ON_UD2
//#define MEMORY_DEBUGGING
//#define DEBUG_WARCRAFT2
//#define DEBUG_BOUND

#ifdef MEMORY_DEBUGGING
static bool shouldLogAllMemoryAccesses(PhysicalAddress address)
{
    UNUSED_PARAM(address);
#ifdef CT_DETERMINISTIC
    return true;
#endif
    return false;
}

static bool shouldLogMemoryWrite(PhysicalAddress address)
{
    if (shouldLogAllMemoryAccesses(address))
        return true;
    return false;
}

static bool shouldLogMemoryRead(PhysicalAddress address)
{
    if (shouldLogAllMemoryAccesses(address))
        return true;
    return false;
}
#endif

CPU* g_cpu = 0;

DWORD CPU::readRegisterForAddressSize(int registerIndex)
{
    if (a32())
        return m_generalPurposeRegister[registerIndex].fullDWORD;
    return m_generalPurposeRegister[registerIndex].lowWORD;
}

void CPU::writeRegisterForAddressSize(int registerIndex, DWORD data)
{
    if (a32())
        m_generalPurposeRegister[registerIndex].fullDWORD = data;
    else
        m_generalPurposeRegister[registerIndex].lowWORD = data;
}

void CPU::stepRegisterForAddressSize(int registerIndex, DWORD stepSize)
{
    if (a32())
        m_generalPurposeRegister[registerIndex].fullDWORD += getDF() ? -stepSize : stepSize;
    else
        m_generalPurposeRegister[registerIndex].lowWORD += getDF() ? -stepSize : stepSize;
}

bool CPU::decrementCXForAddressSize()
{
    if (a32()) {
        setECX(getECX() - 1);
        return getECX() == 0;
    }
    setCX(getCX() - 1);
    return getCX() == 0;
}

template BYTE CPU::readRegister<BYTE>(int) const;
template WORD CPU::readRegister<WORD>(int) const;
template DWORD CPU::readRegister<DWORD>(int) const;
template void CPU::writeRegister<BYTE>(int, BYTE);
template void CPU::writeRegister<WORD>(int, WORD);
template void CPU::writeRegister<DWORD>(int, DWORD);

FLATTEN void CPU::decodeNext()
{
#ifdef CT_TRACE
    if (UNLIKELY(m_isForAutotest))
        dumpTrace();
#endif

#ifdef CRASH_ON_EXECUTE_00000000
    if (UNLIKELY(currentBaseInstructionPointer() == 0 && (getPE() || !getBaseCS()))) {
        dumpAll();
        vlog(LogCPU, "It seems like we've jumped to 00000000 :(");
        ASSERT_NOT_REACHED();
    }
#endif

#ifdef CRASH_ON_VME
    if (UNLIKELY(getVME()))
        ASSERT_NOT_REACHED();
#endif

#ifdef CRASH_ON_PVI
    if (UNLIKELY(getPVI()))
        ASSERT_NOT_REACHED();
#endif

    auto insn = Instruction::fromStream(*this, m_operandSize32, m_addressSize32);
    if (!insn.isValid())
        throw InvalidOpcode();
    execute(insn);
}

FLATTEN void CPU::execute(Instruction& insn)
{
#ifdef CRASH_ON_OPCODE_00_00
    if (UNLIKELY(insn.op() == 0 && insn.rm() == 0)) {
        dumpTrace();
        ASSERT_NOT_REACHED();
    }
#endif

#ifdef DISASSEMBLE_EVERYTHING
    if (options.disassembleEverything)
        vlog(LogCPU, "%s", qPrintable(insn.toString(m_baseEIP, x32())));
#endif
    insn.execute(*this);

    ++m_cycle;
}

void CPU::_RDTSC(Instruction&)
{
    if (getPE() && getCPL() != 0) {
        throw GeneralProtectionFault(0, "RDTSC with CPL != 0");
    }
    setEDX(m_cycle >> 32);
    setEAX(m_cycle);
}

void CPU::_WBINVD(Instruction&)
{
    if (getPE() && getCPL() != 0) {
        throw GeneralProtectionFault(0, "WBINVD");
    }
}

void CPU::_INVLPG(Instruction&)
{
    if (getPE() && getCPL() != 0) {
        throw GeneralProtectionFault(0, "INVLPG");
    }
}

void CPU::_VKILL(Instruction&)
{
    // FIXME: Maybe (0xf1) is a bad choice of opcode here, since that's also INT1 / ICEBP.
    if (!machine().isForAutotest()) {
        throw InvalidOpcode("VKILL (0xf1) is an invalid opcode outside of auto-test mode!");
    }
    vlog(LogCPU, "0xF1: Secret shutdown command received!");
    //dumpAll();
    hard_exit(0);
}

void CPU::setMemorySizeAndReallocateIfNeeded(DWORD size)
{
    if (m_memorySize == size)
        return;
    delete [] m_memory;
    m_memorySize = size;
    m_memory = new BYTE[m_memorySize];
    if (!m_memory) {
        vlog(LogInit, "Insufficient memory available.");
        hard_exit(1);
    }
    memset(m_memory, 0x0, m_memorySize);
}

CPU::CPU(Machine& m)
    : m_machine(m)
{
    m_isForAutotest = machine().isForAutotest();

    buildOpcodeTablesIfNeeded();

    ASSERT(!g_cpu);
    g_cpu = this;

    setMemorySizeAndReallocateIfNeeded(8192 * 1024);

    memset(m_memoryProviders, 0, sizeof(m_memoryProviders));

    m_debugger = make<Debugger>(*this);

    m_controlRegisterMap[0] = &m_CR0;
    m_controlRegisterMap[1] = nullptr;
    m_controlRegisterMap[2] = &m_CR2;
    m_controlRegisterMap[3] = &m_CR3;
    m_controlRegisterMap[4] = &m_CR4;
    m_controlRegisterMap[5] = nullptr;
    m_controlRegisterMap[6] = nullptr;
    m_controlRegisterMap[7] = nullptr;

    m_debugRegisterMap[0] = &m_DR0;
    m_debugRegisterMap[1] = &m_DR1;
    m_debugRegisterMap[2] = &m_DR2;
    m_debugRegisterMap[3] = &m_DR3;
    m_debugRegisterMap[4] = &m_DR4;
    m_debugRegisterMap[5] = &m_DR5;
    m_debugRegisterMap[6] = &m_DR6;
    m_debugRegisterMap[7] = &m_DR7;

    m_byteRegisters[RegisterAH] = &m_generalPurposeRegister[RegisterEAX].highBYTE;
    m_byteRegisters[RegisterBH] = &m_generalPurposeRegister[RegisterEBX].highBYTE;
    m_byteRegisters[RegisterCH] = &m_generalPurposeRegister[RegisterECX].highBYTE;
    m_byteRegisters[RegisterDH] = &m_generalPurposeRegister[RegisterEDX].highBYTE;
    m_byteRegisters[RegisterAL] = &m_generalPurposeRegister[RegisterEAX].lowBYTE;
    m_byteRegisters[RegisterBL] = &m_generalPurposeRegister[RegisterEBX].lowBYTE;
    m_byteRegisters[RegisterCL] = &m_generalPurposeRegister[RegisterECX].lowBYTE;
    m_byteRegisters[RegisterDL] = &m_generalPurposeRegister[RegisterEDX].lowBYTE;

    m_segmentMap[(int)SegmentRegisterIndex::CS] = &this->CS;
    m_segmentMap[(int)SegmentRegisterIndex::DS] = &this->DS;
    m_segmentMap[(int)SegmentRegisterIndex::ES] = &this->ES;
    m_segmentMap[(int)SegmentRegisterIndex::SS] = &this->SS;
    m_segmentMap[(int)SegmentRegisterIndex::FS] = &this->FS;
    m_segmentMap[(int)SegmentRegisterIndex::GS] = &this->GS;
    m_segmentMap[6] = nullptr;
    m_segmentMap[7] = nullptr;

    reset();
}

void CPU::reset()
{
    m_a20Enabled = false;
    m_nextInstructionIsUninterruptible = false;

    memset(&m_generalPurposeRegister, 0, sizeof(m_generalPurposeRegister));
    m_CR0 = 0;
    m_CR2 = 0;
    m_CR3 = 0;
    m_CR4 = 0;
    m_DR0 = 0;
    m_DR1 = 0;
    m_DR2 = 0;
    m_DR3 = 0;
    m_DR4 = 0;
    m_DR5 = 0;
    m_DR6 = 0;
    m_DR7 = 0;

    this->IOPL = 0;
    this->VM = 0;
    this->VIP = 0;
    this->VIF = 0;
    this->NT = 0;
    this->RF = 0;
    this->AC = 0;
    this->ID = 0;

    m_GDTR.clear();
    m_IDTR.clear();
    m_LDTR.clear();

    this->TR.selector = 0;
    this->TR.limit = 0xffff;
    this->TR.base = LinearAddress();
    this->TR.is32Bit = false;

    memset(m_descriptor, 0, sizeof(m_descriptor));

    m_segmentPrefix = SegmentRegisterIndex::None;

    setCS(0);
    setDS(0);
    setES(0);
    setSS(0);
    setFS(0);
    setGS(0);

    if (m_isForAutotest)
        farJump(LogicalAddress(machine().settings().entryCS(), machine().settings().entryIP()), JumpType::Internal);
    else
        farJump(LogicalAddress(0xf000, 0x0000), JumpType::Internal);

    setFlags(0x0200);

    setIOPL(3);

    m_state = Alive;

    m_addressSize32 = false;
    m_operandSize32 = false;
    m_effectiveAddressSize32 = false;
    m_effectiveOperandSize32 = false;

    m_dirtyFlags = 0;
    m_lastResult = 0;
    m_lastOpSize = ByteSize;

    m_cycle = 0;

    initWatches();

    recomputeMainLoopNeedsSlowStuff();
}

CPU::~CPU()
{
    delete [] m_memory;
    m_memory = nullptr;
}

class InstructionExecutionContext {
public:
    InstructionExecutionContext(CPU& cpu) : m_cpu(cpu) { cpu.saveBaseAddress(); }
    ~InstructionExecutionContext() { m_cpu.clearPrefix(); }
private:
    CPU& m_cpu;
};

FLATTEN void CPU::executeOneInstruction()
{
    try {
        InstructionExecutionContext context(*this);
        decodeNext();
    } catch(Exception e) {
        if (options.log_exceptions)
            dumpDisassembled(cachedDescriptor(SegmentRegisterIndex::CS), m_baseEIP, 3);
        raiseException(e);
    } catch(HardwareInterruptDuringREP) {
        setEIP(currentBaseInstructionPointer());
    }
}

void CPU::haltedLoop()
{
    while (state() == CPU::Halted) {
#ifdef HAVE_USLEEP
        usleep(100);
#endif
        if (m_shouldHardReboot) {
            hardReboot();
            return;
        }
        if (debugger().isActive()) {
            saveBaseAddress();
            debugger().doConsole();
        }
        if (PIC::hasPendingIRQ() && getIF())
            PIC::serviceIRQ(*this);
    }
}

void CPU::queueCommand(Command command)
{
    switch (command) {
    case EnterDebugger:
        m_debuggerRequest = PleaseEnterDebugger;
        break;
    case ExitDebugger:
        m_debuggerRequest = PleaseExitDebugger;
        break;
    case HardReboot:
        m_shouldHardReboot = true;
        break;
    }
    recomputeMainLoopNeedsSlowStuff();
}

void CPU::hardReboot()
{
    machine().resetAllIODevices();
    reset();
    m_shouldHardReboot = false;
}

void CPU::makeNextInstructionUninterruptible()
{
    m_nextInstructionIsUninterruptible = true;
}

void CPU::recomputeMainLoopNeedsSlowStuff()
{
    m_mainLoopNeedsSlowStuff = m_debuggerRequest != NoDebuggerRequest ||
                               m_shouldHardReboot ||
                               options.trace ||
                               !m_breakpoints.empty() ||
                               debugger().isActive() ||
                               !m_watches.isEmpty();
}

NEVER_INLINE bool CPU::mainLoopSlowStuff()
{
    if (m_shouldHardReboot) {
        hardReboot();
        return true;
    }

    if (!m_breakpoints.empty()) {
        for (auto& breakpoint : m_breakpoints) {
            if (getCS() == breakpoint.selector() && getEIP() == breakpoint.offset()) {
                debugger().enter();
                break;
            }
        }
    }

    if (m_debuggerRequest == PleaseEnterDebugger) {
        debugger().enter();
        m_debuggerRequest = NoDebuggerRequest;
        recomputeMainLoopNeedsSlowStuff();
    } else if (m_debuggerRequest == PleaseExitDebugger) {
        debugger().exit();
        m_debuggerRequest = NoDebuggerRequest;
        recomputeMainLoopNeedsSlowStuff();
    }

    if (debugger().isActive()) {
        saveBaseAddress();
        debugger().doConsole();
    }

    if (options.trace)
        dumpTrace();

    if (!m_watches.isEmpty())
        dumpWatches();

    return true;
}

FLATTEN void CPU::mainLoop()
{
    forever {
        if (UNLIKELY(m_mainLoopNeedsSlowStuff)) {
            mainLoopSlowStuff();
        }

        executeOneInstruction();

        // FIXME: An obvious optimization here would be to dispatch next insn directly from whoever put us in this state.
        // Easy to implement: just call executeOneInstruction() in e.g "POP SS"
        // I'll do this once things feel more trustworthy in general.
        if (UNLIKELY(m_nextInstructionIsUninterruptible)) {
            m_nextInstructionIsUninterruptible = false;
            continue;
        }

        if (UNLIKELY(getTF())) {
            // The Trap Flag is set, so we'll execute one instruction and
            // call ISR 1 as soon as it's finished.
            //
            // This is used by tools like DEBUG to implement step-by-step
            // execution :-)
            interrupt(1, InterruptSource::Internal);
        }

        if (PIC::hasPendingIRQ() && getIF())
            PIC::serviceIRQ(*this);

#ifdef CT_DETERMINISTIC
        if (getIF() && ((cycle() + 1) % 100 == 0)) {
            machine().pit().raiseIRQ();
        }
#endif
    }
}

void CPU::jumpRelative8(SIGNED_BYTE displacement)
{
    m_EIP += displacement;
}

void CPU::jumpRelative16(SIGNED_WORD displacement)
{
    m_EIP += displacement;
}

void CPU::jumpRelative32(SIGNED_DWORD displacement)
{
    m_EIP += displacement;
}

void CPU::jumpAbsolute16(WORD address)
{
    m_EIP = address;
}

void CPU::jumpAbsolute32(DWORD address)
{
#ifdef CRASH_ON_PE_JMP_00000000
    if (getPE() && !address) {
        vlog(LogCPU, "HMM! Jump to cs:00000000 in PE=1, source: %04x:%08x\n", getBaseCS(), getBaseEIP());
        dumpAll();
        ASSERT_NOT_REACHED();
    }
#endif
//    vlog(LogCPU, "[PE=%u] Abs jump to %08X", getPE(), address);
    m_EIP = address;
}

static const char* toString(JumpType type)
{
    switch (type) {
    case JumpType::CALL: return "CALL";
    case JumpType::RETF: return "RETF";
    case JumpType::IRET: return "IRET";
    case JumpType::INT: return "INT";
    case JumpType::JMP: return "JMP";
    case JumpType::Internal: return "Internal";
    default:
        ASSERT_NOT_REACHED();
        return nullptr;
    }
}

void CPU::realModeFarJump(LogicalAddress address, JumpType type)
{
    ASSERT(!getPE() || getVM());
    WORD selector = address.selector();
    DWORD offset = address.offset();
    WORD originalCS = getCS();
    DWORD originalEIP = getEIP();

#ifdef LOG_FAR_JUMPS
    vlog(LogCPU, "[PE=0] %s from %04x:%08x to %04x:%08x", toString(type), getBaseCS(), currentBaseInstructionPointer(), selector, offset);
#endif

    setCS(selector);
    setEIP(offset);

    if (type == JumpType::CALL) {
#ifdef DEBUG_JUMPS
        vlog(LogCPU, "Push %u-bit cs:eip %04x:%08x @stack{%04x:%08x}", o16() ? 16 : 32, originalCS, originalEIP, getSS(), getESP());
#endif
        pushOperandSizedValue(originalCS);
        pushOperandSizedValue(originalEIP);
    }
}

void CPU::farJump(LogicalAddress address, JumpType type, Gate* gate)
{
    if (!getPE() || getVM()) {
        realModeFarJump(address, type);
    } else {
        protectedModeFarJump(address, type, gate);
    }
}

void CPU::protectedModeFarJump(LogicalAddress address, JumpType type, Gate* gate)
{
    ASSERT(getPE());
    WORD selector = address.selector();
    DWORD offset = address.offset();
    ValueSize pushSize = o32() ? DWordSize : WordSize;

    if (gate) {
        // Coming through a gate; respect bit size of gate descriptor!
        pushSize = gate->is32Bit() ? DWordSize : WordSize;
    }

    WORD originalSS = getSS();
    DWORD originalESP = getESP();
    WORD originalCPL = getCPL();
    WORD originalCS = getCS();
    DWORD originalEIP = getEIP();

    BYTE selectorRPL = selector & 3;

#ifdef LOG_FAR_JUMPS
    vlog(LogCPU, "[PE=%u, PG=%u] %s from %04x:%08x to %04x:%08x", getPE(), getPG(), toString(type), getBaseCS(), currentBaseInstructionPointer(), selector, offset);
#endif

    auto descriptor = getDescriptor(selector);

    if (descriptor.isNull()) {
        throw GeneralProtectionFault(0, QString("%1 to null selector").arg(toString(type)));
    }

    if (descriptor.isOutsideTableLimits())
        throw GeneralProtectionFault(selector & 0xfffc, QString("%1 to selector outside table limit").arg(toString(type)));

    if (!descriptor.isCode() && !descriptor.isCallGate() && !descriptor.isTaskGate() && !descriptor.isTSS())
        throw GeneralProtectionFault(selector & 0xfffc, QString("%1 to invalid descriptor type").arg(toString(type)));

    if (descriptor.isGate() && gate) {
        dumpDescriptor(*gate);
        dumpDescriptor(descriptor);
        throw GeneralProtectionFault(selector & 0xfffc, "Gate-to-gate jumps are not allowed");
    }

    if (descriptor.isTaskGate()) {
        // FIXME: Implement JMP/CALL thorough task gate.
        ASSERT_NOT_REACHED();
    }

    if (descriptor.isCallGate()) {
        auto& gate = descriptor.asGate();
#ifdef DEBUG_JUMPS
        vlog(LogCPU, "Gate (%s) to %04x:%08x (count=%u)", gate.typeName(), gate.selector(), gate.offset(), gate.parameterCount());
#endif
        if (gate.parameterCount() != 0) {
            // FIXME: Implement gate parameter counts.
            ASSERT_NOT_REACHED();
        }

        if (gate.DPL() < getCPL())
            throw GeneralProtectionFault(selector & 0xfffc, QString("%1 to gate with DPL(%2) < CPL(%3)").arg(toString(type)).arg(gate.DPL()).arg(getCPL()));

        if (selectorRPL > gate.DPL())
            throw GeneralProtectionFault(selector & 0xfffc, QString("%1 to gate with RPL(%2) > DPL(%3)").arg(toString(type)).arg(selectorRPL).arg(gate.DPL()));

        if (!gate.present()) {
            throw NotPresent(selector & 0xfffc, QString("Gate not present"));
        }

        // NOTE: We recurse here, jumping to the gate entry point.
        farJump(gate.entry(), type, &gate);
        return;
    }

    if (descriptor.isTSS()) {
        auto& tssDescriptor = descriptor.asTSSDescriptor();
#ifdef DEBUG_JUMPS
        vlog(LogCPU, "CS is this:");
        dumpDescriptor(cachedDescriptor(SegmentRegisterIndex::CS));
        vlog(LogCPU, "%s to TSS descriptor (%s) -> %08x", toString(type), tssDescriptor.typeName(), tssDescriptor.base());
#endif
        if (tssDescriptor.DPL() < getCPL())
            throw GeneralProtectionFault(selector & 0xfffc, QString("%1 to TSS descriptor with DPL < CPL").arg(toString(type)));
        if (tssDescriptor.DPL() < selectorRPL)
            throw GeneralProtectionFault(selector & 0xfffc, QString("%1 to TSS descriptor with DPL < RPL").arg(toString(type)));
        if (!tssDescriptor.present())
            throw NotPresent(selector & 0xfffc, "TSS not present");
        taskSwitch(tssDescriptor, type);
        return;
    }

    // Okay, so it's a code segment then.
    auto& codeSegment = descriptor.asCodeSegmentDescriptor();

    if ((type == JumpType::CALL || type == JumpType::JMP) && !gate) {
        if (codeSegment.conforming()) {
            if (codeSegment.DPL() > getCPL()) {
                throw GeneralProtectionFault(selector & 0xfffc, QString("%1 -> Code segment DPL(%2) > CPL(%3)").arg(toString(type)).arg(codeSegment.DPL()).arg(getCPL()));
            }
        } else {
            if (selectorRPL > codeSegment.DPL()) {
                throw GeneralProtectionFault(selector & 0xfffc, QString("%1 -> Code segment RPL(%2) > CPL(%3)").arg(toString(type)).arg(selectorRPL).arg(codeSegment.DPL()));
            }
            if (codeSegment.DPL() != getCPL()) {
                throw GeneralProtectionFault(selector & 0xfffc, QString("%1 -> Code segment DPL(%2) != CPL(%3)").arg(toString(type)).arg(codeSegment.DPL()).arg(getCPL()));
            }
        }
    }

    if (gate && !gate->is32Bit()) {
        offset &= 0xffff;
    }

    // NOTE: A 32-bit jump into a 16-bit segment might have irrelevant higher bits set.
    // Mask them off to make sure we don't incorrectly fail limit checks.
    if (!codeSegment.is32Bit()) {
        offset &= 0xffff;
    }

    if (!codeSegment.present()) {
        throw NotPresent(selector & 0xfffc, QString("Code segment not present"));
    }

    if (offset > codeSegment.effectiveLimit()) {
        vlog(LogCPU, "%s to eip(%08x) outside limit(%08x)", toString(type), offset, codeSegment.effectiveLimit());
        dumpDescriptor(codeSegment);
        throw GeneralProtectionFault(0, "Offset outside segment limit");
    }

    setCS(selector);
    setEIP(offset);

    if (type == JumpType::CALL && gate) {
    if (descriptor.DPL() < originalCPL) {
#ifdef DEBUG_JUMPS
        vlog(LogCPU, "%s escalating privilege from ring%u to ring%u", toString(type), originalCPL, descriptor.DPL(), descriptor);
#endif
        auto tss = currentTSS();

        WORD newSS = tss.getRingSS(descriptor.DPL());
        DWORD newESP = tss.getRingESP(descriptor.DPL());
        auto newSSDescriptor = getDescriptor(newSS);

        // FIXME: For JumpType::INT, exceptions related to newSS should contain the extra error code.

        if (newSSDescriptor.isNull()) {
            throw InvalidTSS(newSS & 0xfffc, "New ss is null");
        }

        if (newSSDescriptor.isOutsideTableLimits()) {
            throw InvalidTSS(newSS & 0xfffc, "New ss outside table limits");
        }

        if (newSSDescriptor.DPL() != descriptor.DPL()) {
            throw InvalidTSS(newSS & 0xfffc, QString("New ss DPL(%1) != code segment DPL(%2)").arg(newSSDescriptor.DPL()).arg(descriptor.DPL()));
        }

        if (!newSSDescriptor.isData() || !newSSDescriptor.asDataSegmentDescriptor().writable()) {
            throw InvalidTSS(newSS & 0xfffc, "New ss not a writable data segment");
        }

        if (!newSSDescriptor.present()) {
            throw StackFault(newSS & 0xfffc, "New ss not present");
        }

        BEGIN_ASSERT_NO_EXCEPTIONS
        setCPL(descriptor.DPL());
        setSS(newSS);
        setESP(newESP);

#ifdef DEBUG_JUMPS
        vlog(LogCPU, "%s to inner ring, ss:sp %04x:%04x -> %04x:%04x", toString(type), originalSS, originalESP, getSS(), getSP());
        vlog(LogCPU, "Push %u-bit ss:sp %04x:%04x @stack{%04x:%08x}", pushSize, originalSS, originalESP, getSS(), getESP());
#endif
        pushValueWithSize(originalSS, pushSize);
        pushValueWithSize(originalESP, pushSize);
        END_ASSERT_NO_EXCEPTIONS
    } else {
#ifdef DEBUG_JUMPS
        vlog(LogCPU, "%s same privilege from ring%u to ring%u", toString(type), originalCPL, descriptor.DPL());
#endif
        setCPL(originalCPL);
    }
    }

    if (type == JumpType::CALL) {
#ifdef DEBUG_JUMPS
        vlog(LogCPU, "Push %u-bit cs:ip %04x:%04x @stack{%04x:%08x}", pushSize, originalCS, originalEIP, getSS(), getESP());
#endif
        BEGIN_ASSERT_NO_EXCEPTIONS
        pushValueWithSize(originalCS, pushSize);
        pushValueWithSize(originalEIP, pushSize);
        END_ASSERT_NO_EXCEPTIONS
    }

    if (!gate)
        setCPL(originalCPL);
}

void CPU::clearSegmentRegisterAfterReturnIfNeeded(SegmentRegisterIndex segreg, JumpType type)
{
    if (readSegmentRegister(segreg) == 0)
        return;
    auto& cached = cachedDescriptor(segreg);
    if (cached.isNull() || (cached.DPL() < getCPL() && (cached.isData() || cached.isNonconformingCode()))) {
        vlog(LogCPU, "%s clearing %s(%04x) with DPL=%u (CPL now %u)", toString(type), registerName(segreg), readSegmentRegister(segreg), cached.DPL(), getCPL());
        writeSegmentRegister(segreg, 0);
    }
}

void CPU::protectedFarReturn(WORD stackAdjustment)
{
    ASSERT(getPE());
#ifdef DEBUG_JUMPS
    WORD originalSS = getSS();
    DWORD originalESP = getESP();
    WORD originalCS = getCS();
    DWORD originalEIP = getEIP();
#endif

    TransactionalPopper popper(*this);

    DWORD offset = popper.popOperandSizedValue();
    WORD selector = popper.popOperandSizedValue();
    WORD originalCPL = getCPL();
    BYTE selectorRPL = selector & 3;

    popper.adjustStackPointer(stackAdjustment);

#ifdef LOG_FAR_JUMPS
    vlog(LogCPU, "[PE=%u, PG=%u] %s from %04x:%08x to %04x:%08x", getPE(), getPG(), "RETF", getBaseCS(), currentBaseInstructionPointer(), selector, offset);
#endif

    auto descriptor = getDescriptor(selector);

    if (descriptor.isNull())
        throw GeneralProtectionFault(0, "RETF to null selector");

    if (descriptor.isOutsideTableLimits())
        throw GeneralProtectionFault(selector & 0xfffc, "RETF to selector outside table limit");

    if (!descriptor.isCode()) {
        dumpDescriptor(descriptor);
        throw GeneralProtectionFault(selector & 0xfffc, "Not a code segment");
    }

    if (selectorRPL < getCPL())
        throw GeneralProtectionFault(selector & 0xfffc, QString("RETF with RPL(%1) < CPL(%2)").arg(selectorRPL).arg(getCPL()));

    auto& codeSegment = descriptor.asCodeSegmentDescriptor();

    if (codeSegment.conforming() && codeSegment.DPL() > selectorRPL)
        throw GeneralProtectionFault(selector & 0xfffc, "RETF to conforming code segment with DPL > RPL");

    if (!codeSegment.conforming() && codeSegment.DPL() != selectorRPL)
        throw GeneralProtectionFault(selector & 0xfffc, "RETF to non-conforming code segment with DPL != RPL");

    if (!codeSegment.present())
        throw NotPresent(selector & 0xfffc, "Code segment not present");

    // NOTE: A 32-bit jump into a 16-bit segment might have irrelevant higher bits set.
    // Mask them off to make sure we don't incorrectly fail limit checks.
    if (!codeSegment.is32Bit()) {
        offset &= 0xffff;
    }

    if (offset > codeSegment.effectiveLimit()) {
        vlog(LogCPU, "RETF to eip(%08x) outside limit(%08x)", offset, codeSegment.effectiveLimit());
        dumpDescriptor(codeSegment);
        throw GeneralProtectionFault(0, "Offset outside segment limit");
    }

    // FIXME: Validate SS before clobbering CS:EIP.
    setCS(selector);
    setEIP(offset);

    if (selectorRPL > originalCPL) {
        BEGIN_ASSERT_NO_EXCEPTIONS
        DWORD newESP = popper.popOperandSizedValue();
        WORD newSS = popper.popOperandSizedValue();
#ifdef DEBUG_JUMPS
        vlog(LogCPU, "Popped %u-bit ss:esp %04x:%08x @stack{%04x:%08x}", o16() ? 16 : 32, newSS, newESP, getSS(), popper.adjustedStackPointer());
        vlog(LogCPU, "%s from ring%u to ring%u, ss:esp %04x:%08x -> %04x:%08x", "RETF", originalCPL, getCPL(), originalSS, originalESP, newSS, newESP);
#endif

        setSS(newSS);
        setESP(newESP);

        clearSegmentRegisterAfterReturnIfNeeded(SegmentRegisterIndex::ES, JumpType::RETF);
        clearSegmentRegisterAfterReturnIfNeeded(SegmentRegisterIndex::FS, JumpType::RETF);
        clearSegmentRegisterAfterReturnIfNeeded(SegmentRegisterIndex::GS, JumpType::RETF);
        clearSegmentRegisterAfterReturnIfNeeded(SegmentRegisterIndex::DS, JumpType::RETF);
        END_ASSERT_NO_EXCEPTIONS
    } else {
        popper.commit();
    }

    if (getCPL() != originalCPL)
        adjustStackPointer(stackAdjustment);
}

void CPU::realModeFarReturn(WORD stackAdjustment)
{
    DWORD offset = popOperandSizedValue();
    WORD selector = popOperandSizedValue();
    setCS(selector);
    setEIP(offset);
    adjustStackPointer(stackAdjustment);
}

void CPU::farReturn(WORD stackAdjustment)
{
    if (!getPE() || getVM()) {
        realModeFarReturn(stackAdjustment);
        return;
    }

    protectedFarReturn(stackAdjustment);
}

void CPU::setCPL(BYTE cpl)
{
    if (getPE() && !getVM())
        CS = (CS & ~3) | cpl;
    cachedDescriptor(SegmentRegisterIndex::CS).m_RPL = cpl;
}

void CPU::_NOP(Instruction&)
{
}

void CPU::_HLT(Instruction&)
{
    if (getCPL() != 0) {
        throw GeneralProtectionFault(0, QString("HLT with CPL!=0(%1)").arg(getCPL()));
    }

    setState(CPU::Halted);

    if (!getIF()) {
        vlog(LogCPU, "Halted with IF=0");
    } else {
#ifdef VERBOSE_DEBUG
        vlog(LogCPU, "Halted");
#endif
    }

    haltedLoop();
}

void CPU::_XLAT(Instruction&)
{
    setAL(readMemory8(currentSegment(), readRegisterForAddressSize(RegisterBX) + getAL()));
}

void CPU::_XCHG_AX_reg16(Instruction& insn)
{
    auto tmp = insn.reg16();
    insn.reg16() = getAX();
    setAX(tmp);
}

void CPU::_XCHG_EAX_reg32(Instruction& insn)
{
    auto tmp = insn.reg32();
    insn.reg32() = getEAX();
    setEAX(tmp);
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
    setOF(value == (T)std::numeric_limits<typename std::make_signed<T>::type>::min());
    accessor.set(--value);
    adjustFlag(value, value + 1, 1);
    updateFlags<T>(value);
}

template<typename T, class Accessor>
void CPU::doINC(Accessor accessor)
{
    T value = accessor.get();
    setOF(value == (T)std::numeric_limits<typename std::make_signed<T>::type>::max());
    accessor.set(++value);
    adjustFlag(value, value - 1, 1);
    updateFlags<T>(value);
}

void CPU::_DEC_reg16(Instruction& insn)
{
    doDEC<WORD>(RegisterAccessor<WORD>(insn.reg16()));
}

void CPU::_DEC_reg32(Instruction& insn)
{
    doDEC<DWORD>(RegisterAccessor<DWORD>(insn.reg32()));
}

void CPU::_INC_reg16(Instruction& insn)
{
    doINC<WORD>(RegisterAccessor<WORD>(insn.reg16()));
}

void CPU::_INC_reg32(Instruction& insn)
{
    doINC<DWORD>(RegisterAccessor<DWORD>(insn.reg32()));
}

void CPU::_INC_RM16(Instruction& insn)
{
    doINC<WORD>(insn.modrm().accessor16());
}

void CPU::_INC_RM32(Instruction& insn)
{
    doINC<DWORD>(insn.modrm().accessor32());
}

void CPU::_DEC_RM16(Instruction& insn)
{
    doDEC<WORD>(insn.modrm().accessor16());
}

void CPU::_DEC_RM32(Instruction& insn)
{
    doDEC<DWORD>(insn.modrm().accessor32());
}

void CPU::_INC_RM8(Instruction& insn)
{
    doINC<BYTE>(insn.modrm().accessor8());
}

void CPU::_DEC_RM8(Instruction& insn)
{
    doDEC<BYTE>(insn.modrm().accessor8());
}

template<typename T>
void CPU::doLxS(Instruction& insn, SegmentRegisterIndex segreg)
{
    if (insn.modrm().isRegister()) {
        throw InvalidOpcode("LxS with register operand");
    }
    auto address = readLogicalAddress<T>(insn.modrm().segment(), insn.modrm().offset());
    writeSegmentRegister(segreg, address.selector());
    insn.reg<T>() = address.offset();
}

void CPU::_LDS_reg16_mem16(Instruction& insn)
{
    doLxS<WORD>(insn, SegmentRegisterIndex::DS);
}

void CPU::_LDS_reg32_mem32(Instruction& insn)
{
    doLxS<DWORD>(insn, SegmentRegisterIndex::DS);
}

void CPU::_LES_reg16_mem16(Instruction& insn)
{
    doLxS<WORD>(insn, SegmentRegisterIndex::ES);
}

void CPU::_LES_reg32_mem32(Instruction& insn)
{
    doLxS<DWORD>(insn, SegmentRegisterIndex::ES);
}

void CPU::_LFS_reg16_mem16(Instruction& insn)
{
    doLxS<WORD>(insn, SegmentRegisterIndex::FS);
}

void CPU::_LFS_reg32_mem32(Instruction& insn)
{
    doLxS<DWORD>(insn, SegmentRegisterIndex::FS);
}

void CPU::_LSS_reg16_mem16(Instruction& insn)
{
    doLxS<WORD>(insn, SegmentRegisterIndex::SS);
}

void CPU::_LSS_reg32_mem32(Instruction& insn)
{
    doLxS<DWORD>(insn, SegmentRegisterIndex::SS);
}

void CPU::_LGS_reg16_mem16(Instruction& insn)
{
    doLxS<WORD>(insn, SegmentRegisterIndex::GS);
}

void CPU::_LGS_reg32_mem32(Instruction& insn)
{
    doLxS<DWORD>(insn, SegmentRegisterIndex::GS);
}

void CPU::_LEA_reg32_mem32(Instruction& insn)
{
    if (insn.modrm().isRegister()) {
        throw InvalidOpcode("LEA_reg32_mem32 with register source");
    }
    insn.reg32() = insn.modrm().offset();
}

void CPU::_LEA_reg16_mem16(Instruction& insn)
{
    if (insn.modrm().isRegister()) {
        throw InvalidOpcode("LEA_reg16_mem16 with register source");
    }
    insn.reg16() = insn.modrm().offset();
}

static const char* toString(CPU::MemoryAccessType type)
{
    switch (type) {
    case CPU::MemoryAccessType::Read: return "Read";
    case CPU::MemoryAccessType::Write: return "Write";
    case CPU::MemoryAccessType::Execute: return "Execute";
    case CPU::MemoryAccessType::InternalPointer: return "InternalPointer";
    default: return "(wat)";
    }
}

ALWAYS_INLINE PhysicalAddress CPU::translateAddress(LinearAddress linearAddress, MemoryAccessType accessType, BYTE effectiveCPL)
{
    if (!getPE() || !getPG())
        return PhysicalAddress(linearAddress.get());
    return translateAddressSlowCase(linearAddress, accessType, effectiveCPL);
}

static WORD makePFErrorCode(PageFaultFlags::Flags flags, CPU::MemoryAccessType accessType, bool inUserMode)
{
    return flags
         | (accessType == CPU::MemoryAccessType::Write ? PageFaultFlags::Write : PageFaultFlags::Read)
         | (inUserMode ? PageFaultFlags::UserMode : PageFaultFlags::SupervisorMode)
         | (accessType == CPU::MemoryAccessType::Execute ? PageFaultFlags::InstructionFetch : 0);
}

Exception CPU::PageFault(LinearAddress linearAddress, PageFaultFlags::Flags flags, CPU::MemoryAccessType accessType, bool inUserMode, const char* faultTable, DWORD pde, DWORD pte)
{
    WORD error = makePFErrorCode(flags, accessType, inUserMode);
    if (options.log_exceptions) {
        vlog(LogCPU, "Exception: #PF(%04x) %s in %s for %s %s @%08x, PDBR=%08x, PDE=%08x, PTE=%08x",
            error,
            (flags & PageFaultFlags::ProtectionViolation) ? "PV" : "NP",
            faultTable,
            inUserMode ? "User" : "Supervisor",
            toString(accessType),
            linearAddress.get(),
            getCR3(),
            pde,
            pte
        );
    }
    m_CR2 = linearAddress.get();
    if (options.crashOnPF) {
        dumpAll();
        vlog(LogAlert, "CRASH ON #PF");
        ASSERT_NOT_REACHED();
    }
#ifdef DEBUG_WARCRAFT2
    if (getEIP() == 0x100c2f7c) {
        vlog(LogAlert, "CRASH ON specific #PF");
        ASSERT_NOT_REACHED();
    }
#endif
    return Exception(0xe, error, linearAddress.get(), "Page fault");
}

PhysicalAddress CPU::translateAddressSlowCase(LinearAddress linearAddress, MemoryAccessType accessType, BYTE effectiveCPL)
{
    ASSERT(getCR3() < m_memorySize);

    DWORD dir = (linearAddress.get() >> 22) & 0x3FF;
    DWORD page = (linearAddress.get() >> 12) & 0x3FF;
    DWORD offset = linearAddress.get() & 0xFFF;

    ASSERT(!(getCR3() & 0x03ff));

    PhysicalAddress pdeAddress(getCR3() + dir * sizeof(DWORD));
    DWORD pageDirectoryEntry = readPhysicalMemory<DWORD>(pdeAddress);
    PhysicalAddress pteAddress((pageDirectoryEntry & 0xfffff000) + page * sizeof(DWORD));
    DWORD pageTableEntry = readPhysicalMemory<DWORD>(pteAddress);

    bool inUserMode;
    if (effectiveCPL == 0xff)
        inUserMode = getCPL() == 3;
    else
        inUserMode = effectiveCPL == 3;

    if (!(pageDirectoryEntry & PageTableEntryFlags::Present)) {
        throw PageFault(linearAddress, PageFaultFlags::NotPresent, accessType, inUserMode, "PDE", pageDirectoryEntry);
    }

    if (!(pageTableEntry & PageTableEntryFlags::Present)) {
        throw PageFault(linearAddress, PageFaultFlags::NotPresent, accessType, inUserMode, "PTE", pageDirectoryEntry, pageTableEntry);
    }

    if (inUserMode) {
        if (!(pageDirectoryEntry & PageTableEntryFlags::UserSupervisor)) {
            throw PageFault(linearAddress, PageFaultFlags::ProtectionViolation, accessType, inUserMode, "PDE", pageDirectoryEntry);
        }
        if (!(pageTableEntry & PageTableEntryFlags::UserSupervisor)) {
            throw PageFault(linearAddress, PageFaultFlags::ProtectionViolation, accessType, inUserMode, "PTE", pageDirectoryEntry, pageTableEntry);
        }
    }

    if ((inUserMode || getCR0() & CR0::WP) && accessType == MemoryAccessType::Write) {
        if (!(pageDirectoryEntry & PageTableEntryFlags::ReadWrite)) {
            throw PageFault(linearAddress, PageFaultFlags::ProtectionViolation, accessType, inUserMode, "PDE", pageDirectoryEntry);
        }
        if (!(pageTableEntry & PageTableEntryFlags::ReadWrite)) {
            throw PageFault(linearAddress, PageFaultFlags::ProtectionViolation, accessType, inUserMode, "PTE", pageDirectoryEntry, pageTableEntry);
        }
    }

    if (accessType == MemoryAccessType::Write)
        pageTableEntry |= PageTableEntryFlags::Dirty;

    pageDirectoryEntry |= PageTableEntryFlags::Accessed;
    pageTableEntry |= PageTableEntryFlags::Accessed;

    PhysicalAddress physicalAddress((pageTableEntry & 0xfffff000) | offset);
#ifdef DEBUG_PAGING
    vlog(LogCPU, "PG=1 Translating %08x {dir=%03x, page=%03x, offset=%03x} => %08x [%08x + %08x] <PTE @ %08x>", linearAddress.get(), dir, page, offset, physicalAddress.get(), pageDirectoryEntry, pageTableEntry, pteAddress);
#endif
    return physicalAddress;
}

void CPU::snoop(LinearAddress linearAddress, MemoryAccessType accessType)
{
    translateAddress(linearAddress, accessType);
}

void CPU::snoop(SegmentRegisterIndex segreg, DWORD offset, MemoryAccessType accessType)
{
    // FIXME: Support multi-byte snoops.
    if (getPE() && !getVM())
        validateAddress<BYTE>(segreg, offset, accessType);
    auto linearAddress = cachedDescriptor(segreg).linearAddress(offset);
    snoop(linearAddress, accessType);
}

template<typename T>
ALWAYS_INLINE void CPU::validateAddress(const SegmentDescriptor& descriptor, DWORD offset, MemoryAccessType accessType)
{
    if (!getVM()) {
    if (accessType != MemoryAccessType::Execute) {
        if (descriptor.isNull()) {
            vlog(LogAlert, "NULL! %s offset %08X into null selector (selector index: %04X)",
                 toString(accessType),
                 offset,
                 descriptor.index());
            throw GeneralProtectionFault(0, "Access through null selector");
        }
    }

    switch (accessType) {
    case MemoryAccessType::Read:
        if (descriptor.isCode() && !descriptor.asCodeSegmentDescriptor().readable()) {
            throw GeneralProtectionFault(0, "Attempt to read from non-readable code segment");
        }
        break;
    case MemoryAccessType::Write:
        if (!descriptor.isData()) {
            throw GeneralProtectionFault(0, "Attempt to write to non-data segment");
        }
        if (!descriptor.asDataSegmentDescriptor().writable()) {
            throw GeneralProtectionFault(0, "Attempt to write to non-writable data segment");
        }
        break;
    case MemoryAccessType::Execute:
        // CS should never point to a non-code segment.
        ASSERT(descriptor.isCode());
        break;
    default:
        break;
    }
    }

#if 0
    // FIXME: Is this appropriate somehow? Need to figure it out. The code below as-is breaks IRET.
    if (getCPL() > descriptor.DPL()) {
        throw GeneralProtectionFault(0, QString("Insufficient privilege for access (CPL=%1, DPL=%2)").arg(getCPL()).arg(descriptor.DPL()));
    }
#endif

    if (UNLIKELY(offset > descriptor.effectiveLimit())) {
        vlog(LogAlert, "FUG! %s offset %08X outside limit (selector index: %04X, effective limit: %08X [%08X x %s])",
             toString(accessType),
             offset,
             descriptor.index(),
             descriptor.effectiveLimit(),
             descriptor.limit(),
             descriptor.granularity() ? "4K" : "1b"
             );
        //ASSERT_NOT_REACHED();
        dumpDescriptor(descriptor);
        //dumpAll();
        //debugger().enter();
        throw GeneralProtectionFault(descriptor.index(), "Access outside segment limit");
    }
}

template<typename T>
ALWAYS_INLINE void CPU::validateAddress(SegmentRegisterIndex segreg, DWORD offset, MemoryAccessType accessType)
{
    validateAddress<T>(cachedDescriptor(segreg), offset, accessType);
}

template<typename T>
bool CPU::validatePhysicalAddress(PhysicalAddress physicalAddress, MemoryAccessType accessType)
{
    UNUSED_PARAM(accessType);
    if (physicalAddress.get() < m_memorySize)
        return true;
#ifdef MEMORY_DEBUGGING
    if (options.memdebug) {
        vlog(LogCPU, "OOB %zu-bit %s access @ physical %08x [A20=%s] [PG=%u]",
            sizeof(T) * 8,
            toString(accessType),
            physicalAddress.get(),
            isA20Enabled() ? "on" : "off",
            getPG()
        );
    }
#endif
    return false;
}

template<typename T>
T CPU::readPhysicalMemory(PhysicalAddress physicalAddress)
{
    if (!validatePhysicalAddress<T>(physicalAddress, MemoryAccessType::Read)) {
#ifdef DEBUG_PHYSICAL_OOB
        vlog(LogCPU, "Read outside physical memory: %08x", physicalAddress);
        debugger().enter();
#endif
        return 0;
    }
    if (auto* provider = memoryProviderForAddress(physicalAddress)) {
        if (auto* directReadAccessPointer = provider->pointerForDirectReadAccess()) {
            return *reinterpret_cast<const T*>(&directReadAccessPointer[physicalAddress.get() - provider->baseAddress().get()]);
        }
        return provider->read<T>(physicalAddress.get());
    }
    return *reinterpret_cast<T*>(&m_memory[physicalAddress.get()]);
}

template BYTE CPU::readPhysicalMemory<BYTE>(PhysicalAddress);
template WORD CPU::readPhysicalMemory<WORD>(PhysicalAddress);
template DWORD CPU::readPhysicalMemory<DWORD>(PhysicalAddress);

template<typename T>
void CPU::writePhysicalMemory(PhysicalAddress physicalAddress, T data)
{
    if (!validatePhysicalAddress<T>(physicalAddress, MemoryAccessType::Write)) {
#ifdef DEBUG_PHYSICAL_OOB
        vlog(LogCPU, "Write outside physical memory: %08x", physicalAddress);
        debugger().enter();
#endif
        return;
    }
    if (auto* provider = memoryProviderForAddress(physicalAddress)) {
        provider->write<T>(physicalAddress.get(), data);
    } else {
        *reinterpret_cast<T*>(&m_memory[physicalAddress.get()]) = data;
    }
}

template void CPU::writePhysicalMemory<BYTE>(PhysicalAddress, BYTE);
template void CPU::writePhysicalMemory<WORD>(PhysicalAddress, WORD);
template void CPU::writePhysicalMemory<DWORD>(PhysicalAddress, DWORD);

template<typename T>
ALWAYS_INLINE T CPU::readMemory(LinearAddress linearAddress, MemoryAccessType accessType, BYTE effectiveCPL)
{
    // FIXME: This needs to be optimized.
    if constexpr (sizeof(T) == 4) {
        if (getPG() && (linearAddress.get() & 0xfffff000) != (((linearAddress.get() + (sizeof(T) - 1)) & 0xfffff000))) {
            BYTE b1 = readMemory<BYTE>(linearAddress.offset(0), accessType, effectiveCPL);
            BYTE b2 = readMemory<BYTE>(linearAddress.offset(1), accessType, effectiveCPL);
            BYTE b3 = readMemory<BYTE>(linearAddress.offset(2), accessType, effectiveCPL);
            BYTE b4 = readMemory<BYTE>(linearAddress.offset(3), accessType, effectiveCPL);
            return weld<DWORD>(weld<WORD>(b4, b3), weld<WORD>(b2, b1));
        }
    } else if constexpr (sizeof(T) == 2) {
        if (getPG() && (linearAddress.get() & 0xfffff000) != (((linearAddress.get() + (sizeof(T) - 1)) & 0xfffff000))) {
            BYTE b1 = readMemory<BYTE>(linearAddress.offset(0), accessType, effectiveCPL);
            BYTE b2 = readMemory<BYTE>(linearAddress.offset(1), accessType, effectiveCPL);
            return weld<WORD>(b2, b1);
        }
    }

    auto physicalAddress = translateAddress(linearAddress, accessType, effectiveCPL);
#ifdef A20_ENABLED
    physicalAddress.mask(a20Mask());
#endif
    T value = readPhysicalMemory<T>(physicalAddress);
#ifdef MEMORY_DEBUGGING
    if (options.memdebug || shouldLogMemoryRead(physicalAddress)) {
        if (options.novlog)
            printf("%04X:%08X: %zu-bit read [A20=%s] 0x%08X, value: %08X\n", getBaseCS(), currentBaseInstructionPointer(), sizeof(T) * 8, isA20Enabled() ? "on" : "off", physicalAddress.get(), value);
        else
            vlog(LogCPU, "%zu-bit read [A20=%s] 0x%08X, value: %08X", sizeof(T) * 8, isA20Enabled() ? "on" : "off", physicalAddress.get(), value);
    }
#endif
    return value;
}

template<typename T>
ALWAYS_INLINE T CPU::readMemory(const SegmentDescriptor& descriptor, DWORD offset, MemoryAccessType accessType)
{
    auto linearAddress = descriptor.linearAddress(offset);
    if (getPE() && !getVM())
        validateAddress<T>(descriptor, offset, accessType);
    return readMemory<T>(linearAddress, accessType);
}

template<typename T>
ALWAYS_INLINE T CPU::readMemory(SegmentRegisterIndex segreg, DWORD offset, MemoryAccessType accessType)
{
    return readMemory<T>(cachedDescriptor(segreg), offset, accessType);
}

template<typename T>
ALWAYS_INLINE T CPU::readMemoryMetal(LinearAddress laddr)
{
    return readMemory<T>(laddr, MemoryAccessType::Read, 0);
}

template<typename T>
ALWAYS_INLINE void CPU::writeMemoryMetal(LinearAddress laddr, T value)
{
    return writeMemory<T>(laddr, value, 0);
}

template BYTE CPU::readMemory<BYTE>(SegmentRegisterIndex, DWORD, MemoryAccessType);
template WORD CPU::readMemory<WORD>(SegmentRegisterIndex, DWORD, MemoryAccessType);
template DWORD CPU::readMemory<DWORD>(SegmentRegisterIndex, DWORD, MemoryAccessType);

template void CPU::writeMemory<BYTE>(SegmentRegisterIndex, DWORD, BYTE);
template void CPU::writeMemory<WORD>(SegmentRegisterIndex, DWORD, WORD);
template void CPU::writeMemory<DWORD>(SegmentRegisterIndex, DWORD, DWORD);

template void CPU::writeMemory<BYTE>(LinearAddress, BYTE, BYTE);

template WORD CPU::readMemoryMetal<WORD>(LinearAddress);
template DWORD CPU::readMemoryMetal<DWORD>(LinearAddress);

BYTE CPU::readMemory8(LinearAddress address) { return readMemory<BYTE>(address); }
WORD CPU::readMemory16(LinearAddress address) { return readMemory<WORD>(address); }
DWORD CPU::readMemory32(LinearAddress address) { return readMemory<DWORD>(address); }
WORD CPU::readMemoryMetal16(LinearAddress address) { return readMemoryMetal<WORD>(address); }
DWORD CPU::readMemoryMetal32(LinearAddress address) { return readMemoryMetal<DWORD>(address); }
BYTE CPU::readMemory8(SegmentRegisterIndex segment, DWORD offset) { return readMemory<BYTE>(segment, offset); }
WORD CPU::readMemory16(SegmentRegisterIndex segment, DWORD offset) { return readMemory<WORD>(segment, offset); }
DWORD CPU::readMemory32(SegmentRegisterIndex segment, DWORD offset) { return readMemory<DWORD>(segment, offset); }

template<typename T>
LogicalAddress CPU::readLogicalAddress(SegmentRegisterIndex segreg, DWORD offset)
{
    LogicalAddress address;
    address.setOffset(readMemory<T>(segreg, offset));
    address.setSelector(readMemory16(segreg, offset + sizeof(T)));
    return address;
}

template LogicalAddress CPU::readLogicalAddress<WORD>(SegmentRegisterIndex, DWORD);
template LogicalAddress CPU::readLogicalAddress<DWORD>(SegmentRegisterIndex, DWORD);


template<typename T>
void CPU::writeMemory(LinearAddress linearAddress, T value, BYTE effectiveCPL)
{
    // FIXME: This needs to be optimized.
    if constexpr (sizeof(T) == 4) {
        if (getPG() && (linearAddress.get() & 0xfffff000) != (((linearAddress.get() + (sizeof(T) - 1)) & 0xfffff000))) {
            writeMemory<BYTE>(linearAddress.offset(0), value & 0xff, effectiveCPL);
            writeMemory<BYTE>(linearAddress.offset(1), (value >> 8) & 0xff , effectiveCPL);
            writeMemory<BYTE>(linearAddress.offset(2), (value >> 16) & 0xff, effectiveCPL);
            writeMemory<BYTE>(linearAddress.offset(3), (value >> 24) & 0xff, effectiveCPL);
            return;
        }
    } else if constexpr (sizeof(T) == 2) {
        if (getPG() && (linearAddress.get() & 0xfffff000) != (((linearAddress.get() + (sizeof(T) - 1)) & 0xfffff000))) {
            writeMemory<BYTE>(linearAddress.offset(0), value & 0xff, effectiveCPL);
            writeMemory<BYTE>(linearAddress.offset(1), (value >> 8) & 0xff , effectiveCPL);
            return;
        }
    }

    auto physicalAddress = translateAddress(linearAddress, MemoryAccessType::Write, effectiveCPL);
#ifdef A20_ENABLED
    physicalAddress.mask(a20Mask());
#endif
#ifdef MEMORY_DEBUGGING
    if (options.memdebug || shouldLogMemoryWrite(physicalAddress)) {
        if (options.novlog)
            printf("%04X:%08X: %zu-bit write [A20=%s] 0x%08X, value: %08X\n", getBaseCS(), currentBaseInstructionPointer(), sizeof(T) * 8, isA20Enabled() ? "on" : "off", physicalAddress.get(), value);
        else
            vlog(LogCPU, "%zu-bit write [A20=%s] 0x%08X, value: %08X", sizeof(T) * 8, isA20Enabled() ? "on" : "off", physicalAddress.get(), value);
    }
#endif
    writePhysicalMemory(physicalAddress, value);
}

template<typename T>
void CPU::writeMemory(const SegmentDescriptor& descriptor, DWORD offset, T value)
{
    auto linearAddress = descriptor.linearAddress(offset);
    if (getPE() && !getVM())
        validateAddress<T>(descriptor, offset, MemoryAccessType::Write);
    writeMemory(linearAddress, value);
}

template<typename T>
void CPU::writeMemory(SegmentRegisterIndex segreg, DWORD offset, T value)
{
    return writeMemory<T>(cachedDescriptor(segreg), offset, value);
}

void CPU::writeMemory8(LinearAddress address, BYTE value) { writeMemory(address, value); }
void CPU::writeMemory16(LinearAddress address, WORD value) { writeMemory(address, value); }
void CPU::writeMemory32(LinearAddress address, DWORD value) { writeMemory(address, value); }
void CPU::writeMemoryMetal16(LinearAddress address, WORD value) { writeMemoryMetal(address, value); }
void CPU::writeMemoryMetal32(LinearAddress address, DWORD value) { writeMemoryMetal(address, value); }
void CPU::writeMemory8(SegmentRegisterIndex segment, DWORD offset, BYTE value) { writeMemory(segment, offset, value); }
void CPU::writeMemory16(SegmentRegisterIndex segment, DWORD offset, WORD value) { writeMemory(segment, offset, value); }
void CPU::writeMemory32(SegmentRegisterIndex segment, DWORD offset, DWORD value) { writeMemory(segment, offset, value); }

void CPU::updateDefaultSizes()
{
#ifdef VERBOSE_DEBUG
    bool oldO32 = m_operandSize32;
    bool oldA32 = m_addressSize32;
#endif

    auto& csDescriptor = cachedDescriptor(SegmentRegisterIndex::CS);
    m_addressSize32 = csDescriptor.D();
    m_operandSize32 = csDescriptor.D();

#ifdef VERBOSE_DEBUG
    if (oldO32 != m_operandSize32 || oldA32 != m_addressSize32) {
        vlog(LogCPU, "updateDefaultSizes PE=%u X:%u O:%u A:%u (newCS: %04X)", getPE(), x16() ? 16 : 32, o16() ? 16 : 32, a16() ? 16 : 32, getCS());
        dumpDescriptor(csDescriptor);
    }
#endif
}

void CPU::updateStackSize()
{
#ifdef VERBOSE_DEBUG
    bool oldS32 = m_stackSize32;
#endif

    auto& ssDescriptor = cachedDescriptor(SegmentRegisterIndex::SS);
    m_stackSize32 = ssDescriptor.D();

#ifdef VERBOSE_DEBUG
    if (oldS32 != m_stackSize32) {
        vlog(LogCPU, "updateStackSize PE=%u S:%u (newSS: %04x)", getPE(), s16() ? 16 : 32, getSS());
        dumpDescriptor(ssDescriptor);
    }
#endif
}

void CPU::updateCodeSegmentCache()
{
    // FIXME: We need some kind of fast pointer for fetching from CS:EIP.
}

void CPU::setCS(WORD value)
{
    writeSegmentRegister(SegmentRegisterIndex::CS, value);
}

void CPU::setDS(WORD value)
{
    writeSegmentRegister(SegmentRegisterIndex::DS, value);
}

void CPU::setES(WORD value)
{
    writeSegmentRegister(SegmentRegisterIndex::ES, value);
}

void CPU::setSS(WORD value)
{
    writeSegmentRegister(SegmentRegisterIndex::SS, value);
}

void CPU::setFS(WORD value)
{
    writeSegmentRegister(SegmentRegisterIndex::FS, value);
}

void CPU::setGS(WORD value)
{
    writeSegmentRegister(SegmentRegisterIndex::GS, value);
}

const BYTE* CPU::pointerToPhysicalMemory(PhysicalAddress physicalAddress)
{
    if (!validatePhysicalAddress<BYTE>(physicalAddress, MemoryAccessType::InternalPointer))
        return nullptr;
    if (auto* provider = memoryProviderForAddress(physicalAddress))
        return provider->memoryPointer(physicalAddress.get());
    return &m_memory[physicalAddress.get()];
}

const BYTE* CPU::memoryPointer(SegmentRegisterIndex segreg, DWORD offset)
{
    return memoryPointer(cachedDescriptor(segreg), offset);
}

const BYTE* CPU::memoryPointer(const SegmentDescriptor& descriptor, DWORD offset)
{
    auto linearAddress = descriptor.linearAddress(offset);
    if (getPE() && !getVM())
        validateAddress<BYTE>(descriptor, offset, MemoryAccessType::InternalPointer);
    return memoryPointer(linearAddress);
}

const BYTE* CPU::memoryPointer(LogicalAddress address)
{
    return memoryPointer(getSegmentDescriptor(address.selector()), address.offset());
}

const BYTE* CPU::memoryPointer(LinearAddress linearAddress)
{
    auto physicalAddress = translateAddress(linearAddress, MemoryAccessType::InternalPointer);
#ifdef A20_ENABLED
    physicalAddress.mask(a20Mask());
#endif
    return pointerToPhysicalMemory(physicalAddress);
}

template<typename T>
ALWAYS_INLINE T CPU::readInstructionStream()
{
    T data = readMemory<T>(SegmentRegisterIndex::CS, currentInstructionPointer(), MemoryAccessType::Execute);
    adjustInstructionPointer(sizeof(T));
    return data;
}

BYTE CPU::readInstruction8()
{
    return readInstructionStream<BYTE>();
}

WORD CPU::readInstruction16()
{
    return readInstructionStream<WORD>();
}

DWORD CPU::readInstruction32()
{
    return readInstructionStream<DWORD>();
}

void CPU::_CPUID(Instruction&)
{
    if (getEAX() == 0) {
        setEAX(1);
        setEBX(0x706d6f43);
        setEDX(0x6f727475);
        setECX(0x3638586e);
        return;
    }

    if (getEAX() == 1) {
        DWORD stepping = 0;
        DWORD model = 1;
        DWORD family = 3;
        DWORD type = 0;
        setEAX(stepping | (model << 4) | (family << 8) | (type << 12));
        return;
    }

    if (getEAX() == 0x80000000) {
        setEAX(0x80000004);
        return;
    }

    if (getEAX() == 0x80000002) {
        setEAX(0x61632049);
        setEBX(0x2074276e);
        setECX(0x696c6562);
        setEDX(0x20657665);
        return;
    }

    if (getEAX() == 0x80000003) {
        setEAX(0x73277469);
        setEBX(0x746f6e20);
        setECX(0x746e4920);
        setEDX(0x00216c65);
        return;
    }

    if (getEAX() == 0x80000004) {
        setEAX(0);
        setEBX(0);
        setECX(0);
        setEDX(0);
        return;
    }
}

void CPU::_LOCK(Instruction&)
{
    ASSERT_NOT_REACHED();
}

void CPU::initWatches()
{
}

void CPU::registerMemoryProvider(MemoryProvider& provider)
{
    if ((provider.baseAddress().get() + provider.size()) > 1048576) {
        vlog(LogConfig, "Can't register mapper with length %u @ %08x", provider.size(), provider.baseAddress().get());
        ASSERT_NOT_REACHED();
    }

    for (unsigned i = provider.baseAddress().get() / memoryProviderBlockSize; i < (provider.baseAddress().get() + provider.size()) / memoryProviderBlockSize; ++i) {
        vlog(LogConfig, "Register memory provider %p as mapper %u", &provider, i);
        m_memoryProviders[i] = &provider;
    }
}

ALWAYS_INLINE MemoryProvider* CPU::memoryProviderForAddress(PhysicalAddress address)
{
    if (address.get() >= 1048576)
        return nullptr;
    return m_memoryProviders[address.get() / memoryProviderBlockSize];
}

template<typename T>
void CPU::doBOUND(Instruction& insn)
{
    if (insn.modrm().isRegister()) {
        throw InvalidOpcode("BOUND with register operand");
    }
    T arrayIndex = insn.reg32();
    T lowerBound = readMemory<T>(insn.modrm().segment(), insn.modrm().offset());
    T upperBound = readMemory<T>(insn.modrm().segment(), insn.modrm().offset() + sizeof(T));
    bool isWithinBounds = arrayIndex >= lowerBound && arrayIndex <= upperBound;
#ifdef DEBUG_BOUND
    vlog(LogCPU, "BOUND<%u> checking if %d is within [%d, %d]: %s",
        sizeof(T) * 8,
        arrayIndex,
        lowerBound,
        upperBound,
        isWithinBounds ? "yes" : "no");
#endif
    if (!isWithinBounds)
        throw BoundRangeExceeded(QString("%1 not within [%2, %3]").arg(arrayIndex).arg(lowerBound).arg(upperBound));
}

void CPU::_BOUND(Instruction& insn)
{
    if (o16())
        doBOUND<SIGNED_WORD>(insn);
    else
        doBOUND<SIGNED_DWORD>(insn);
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
