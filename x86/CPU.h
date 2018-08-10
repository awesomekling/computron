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
#include <QtCore/QVector>
#include <set>
#include "OwnPtr.h"
#include "Instruction.h"
#include "Descriptor.h"

class Debugger;
class Machine;
class MemoryProvider;
class CPU;
class TSS;

struct WatchedAddress {
    WatchedAddress() { }
    WatchedAddress(QString n, DWORD a, ValueSize s, bool b = false) : name(n), address(a), size(s), breakOnChange(b) { }
    QString name;
    PhysicalAddress address { 0xBEEFBABE };
    ValueSize size { ByteSize };
    bool breakOnChange { false };
    static const QWORD neverSeen = 0xFFFFFFFFFFFFFFFF;
    QWORD lastSeenValue { neverSeen };
};

enum class JumpType { Internal, IRET, RETF, INT, CALL, JMP };


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

struct HardwareInterruptDuringREP { };

class Exception {
public:
    Exception(BYTE num, WORD code, DWORD address, const QString& reason)
        : m_num(num)
        , m_code(code)
        , m_address(address)
        , m_hasCode(true)
        , m_reason(reason)
    {
    }

    Exception(BYTE num, WORD code, const QString& reason)
        : m_num(num)
        , m_code(code)
        , m_hasCode(true)
        , m_reason(reason)
    {
    }

    Exception(BYTE num, const QString& reason)
        : m_num(num)
        , m_hasCode(false)
        , m_reason(reason)
    {
    }

    ~Exception() { }

    BYTE num() const { return m_num; }
    WORD code() const { return m_code; }
    bool hasCode() const { return m_hasCode; }
    DWORD address() const { return m_address; }
    QString reason() const { return m_reason; }

private:
    BYTE m_num { 0 };
    WORD m_code { 0 };
    DWORD m_address { 0 };
    bool m_hasCode { false };
    QString m_reason;
};

union PartAddressableRegister {
    struct {
        DWORD fullDWORD;
    };
#ifdef CT_BIG_ENDIAN
    struct {
        WORD __highWORD;
        WORD lowWORD;
    };
    struct {
        WORD __highWORD2;
        BYTE highBYTE;
        BYTE lowBYTE;
    };
#else
    struct {
        WORD lowWORD;
        WORD __highWORD;
    };
    struct {
        BYTE lowBYTE;
        BYTE highBYTE;
        BYTE __highWORD2;
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
    WORD limit() const { return m_limit; }
    WORD selector() const { return m_selector; }

    void setBase(LinearAddress address) { m_base = address; }
    void setLimit(WORD limit) { m_limit = limit; }
    void setSelector(WORD selector) { m_selector = selector; }

    void clear()
    {
        m_base = LinearAddress();
        m_limit = 0xffff;
        m_selector = 0;
    }

private:
    const char* m_name { nullptr };
    LinearAddress m_base { 0 };
    WORD m_limit { 0xffff };
    WORD m_selector { 0 };
};

class CPU final : public InstructionStream {
    friend void buildOpcodeTablesIfNeeded();
    friend class Debugger;
public:
    explicit CPU(Machine&);
    ~CPU();

    struct Flag {
    enum Flags {
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
    enum Bits {
        PE = 1 << 0,
        EM = 1 << 2,
        TS = 1 << 3,
        WP = 1 << 16,
        PG = 1 << 31,
    };
    };

    struct CR4 {
    enum Bits {
        VME = 1 << 0,
        PVI = 1 << 1,
    };
    };

    void registerMemoryProvider(MemoryProvider&);
    MemoryProvider* memoryProviderForAddress(PhysicalAddress);

    void recomputeMainLoopNeedsSlowStuff();

    QWORD cycle() const { return m_cycle; }

    void reset();

    Machine& machine() const { return m_machine; }

    std::set<LogicalAddress>& breakpoints() { return m_breakpoints; }

    enum class MemoryAccessType { Read, Write, Execute, InternalPointer };

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
        TransactionalPopper(CPU& cpu) : m_cpu(cpu) { }
        ~TransactionalPopper() { }

        // FIXME: Check SS limits as we go.
        void commit() { m_cpu.adjustStackPointer(m_offset); }

        DWORD pop32() { auto data = m_cpu.readMemory32(SegmentRegisterIndex::SS, m_cpu.currentStackPointer() + m_offset); m_offset += 4; return data; }
        WORD pop16() { auto data = m_cpu.readMemory16(SegmentRegisterIndex::SS, m_cpu.currentStackPointer() + m_offset); m_offset += 2; return data; }
        DWORD popOperandSizedValue() { return m_cpu.o16() ? pop16() : pop32(); }
        void adjustStackPointer(int adjustment) { m_offset += adjustment; }
        DWORD adjustedStackPointer() const { return m_cpu.currentStackPointer() + m_offset; }

    private:
        CPU& m_cpu;
        int m_offset { 0 };
    };

    void dumpSegment(WORD index);
    void dumpDescriptor(const Descriptor&, const char* prefix = "");
    void dumpDescriptor(const Gate&, const char* prefix = "");
    void dumpDescriptor(const SegmentDescriptor&, const char* prefix = "");
    void dumpDescriptor(const SystemDescriptor&, const char* prefix = "");
    void dumpDescriptor(const CodeSegmentDescriptor&, const char* prefix = "");
    void dumpDescriptor(const DataSegmentDescriptor&, const char* prefix = "");

    LogicalAddress getRealModeInterruptVector(BYTE index);
    SegmentDescriptor getRealModeOrVM86Descriptor(WORD selector, SegmentRegisterIndex = SegmentRegisterIndex::None);
    Descriptor getDescriptor(WORD selector);
    SegmentDescriptor getSegmentDescriptor(WORD selector);
    Descriptor getInterruptDescriptor(BYTE number);
    Descriptor getDescriptor(DescriptorTableRegister&, WORD index, bool indexIsSelector);

    SegmentRegisterIndex currentSegment() const { return m_segmentPrefix == SegmentRegisterIndex::None ? SegmentRegisterIndex::DS : m_segmentPrefix; }
    bool hasSegmentPrefix() const { return m_segmentPrefix != SegmentRegisterIndex::None; }

    void setSegmentPrefix(SegmentRegisterIndex segment)
    {
        m_segmentPrefix = segment;
    }

    void clearPrefix()
    {
        m_segmentPrefix = SegmentRegisterIndex::None;
        m_effectiveAddressSize32 = m_addressSize32;
        m_effectiveOperandSize32 = m_operandSize32;
    }

    // Extended memory size in KiB (will be reported by CMOS)
    DWORD extendedMemorySize() const { return m_extendedMemorySize; }
    void setExtendedMemorySize(DWORD size) { m_extendedMemorySize = size; }

    // Conventional memory size in KiB (will be reported by CMOS)
    DWORD baseMemorySize() const { return m_baseMemorySize; }
    void setBaseMemorySize(DWORD size) { m_baseMemorySize = size; }

    void setMemorySizeAndReallocateIfNeeded(DWORD);

    void kill();

    void setA20Enabled(bool value) { m_a20Enabled = value; }
    bool isA20Enabled() const { return m_a20Enabled; }

    DWORD a20Mask() const { return isA20Enabled() ? 0xFFFFFFFF : 0xFFEFFFFF; }

    enum class InterruptSource { Internal = 0, External = 1 };

    void realModeInterrupt(BYTE isr, InterruptSource);
    void protectedModeInterrupt(BYTE isr, InterruptSource, QVariant errorCode);
    void interrupt(BYTE isr, InterruptSource, QVariant errorCode = QVariant());
    void interruptToTaskGate(BYTE isr, InterruptSource, QVariant errorCode, Gate&);

    void interruptFromVM86Mode(Gate&, DWORD offset, CodeSegmentDescriptor&, InterruptSource, QVariant errorCode);
    void iretToVM86Mode(TransactionalPopper&, LogicalAddress, DWORD flags);
    void iretFromVM86Mode();
    void iretFromRealMode();

    Exception GeneralProtectionFault(WORD selector, const QString& reason);
    Exception StackFault(WORD selector, const QString& reason);
    Exception NotPresent(WORD selector, const QString& reason);
    Exception InvalidTSS(WORD selector, const QString& reason);
    Exception PageFault(LinearAddress, PageFaultFlags::Flags, MemoryAccessType, bool inUserMode, const char* faultTable, DWORD pde, DWORD pte = 0);
    Exception DivideError(const QString& reason);
    Exception InvalidOpcode(const QString& reason = QString());
    Exception BoundRangeExceeded(const QString& reason);

    void raiseException(const Exception&);

    void setIF(bool value) { this->IF = value; }
    void setCF(bool value) { this->CF = value; }
    void setDF(bool value) { this->DF = value; }
    void setSF(bool value) { m_dirtyFlags &= ~Flag::SF; this->SF = value; }
    void setAF(bool value) { this->AF = value; }
    void setTF(bool value) { this->TF = value; }
    void setOF(bool value) { this->OF = value; }
    void setPF(bool value) { m_dirtyFlags &= ~Flag::PF; this->PF = value; }
    void setZF(bool value) { m_dirtyFlags &= ~Flag::ZF; this->ZF = value; }
    void setVIF(bool value) { this->VIF = value; }
    void setNT(bool value) { this->NT = value; }
    void setRF(bool value) { this->RF = value; }
    void setVM(bool value) { this->VM = value; }
    void setIOPL(unsigned int value) { this->IOPL = value; }

    bool getIF() const { return this->IF; }
    bool getCF() const { return this->CF; }
    bool getDF() const { return this->DF; }
    bool getSF() const;
    bool getAF() const { return this->AF; }
    bool getTF() const { return this->TF; }
    bool getOF() const { return this->OF; }
    bool getPF() const;
    bool getZF() const;

    unsigned int getIOPL() const { return this->IOPL; }

    BYTE getCPL() const { return cachedDescriptor(SegmentRegisterIndex::CS).RPL(); }
    void setCPL(BYTE);

    bool getNT() const { return this->NT; }
    bool getVIP() const { return this->VIP; }
    bool getVIF() const { return this->VIF; }
    bool getVM() const { return this->VM; }
    bool getPE() const { return m_CR0 & CR0::PE; }
    bool getPG() const { return m_CR0 & CR0::PG; }
    bool getVME() const { return m_CR4 & CR4::VME; }
    bool getPVI() const { return m_CR4 & CR4::PVI; }

    WORD getCS() const { return this->CS; }
    WORD getIP() const { return m_EIP & 0xffff; }
    DWORD getEIP() const { return m_EIP; }

    WORD getDS() const { return this->DS; }
    WORD getES() const { return this->ES; }
    WORD getSS() const { return this->SS; }
    WORD getFS() const { return this->FS; }
    WORD getGS() const { return this->GS; }

    void setCS(WORD cs);
    void setDS(WORD ds);
    void setES(WORD es);
    void setSS(WORD ss);
    void setFS(WORD fs);
    void setGS(WORD gs);

    void setIP(WORD ip) { setEIP(ip); }
    void setEIP(DWORD eip) { m_EIP = eip; }

    WORD readSegmentRegister(SegmentRegisterIndex segreg) const { return *m_segmentMap[static_cast<int>(segreg)]; }

    DWORD getControlRegister(int registerIndex) const { return *m_controlRegisterMap[registerIndex]; }
    void setControlRegister(int registerIndex, DWORD value) { *m_controlRegisterMap[registerIndex] = value; }

    DWORD getDebugRegister(int registerIndex) const { return *m_debugRegisterMap[registerIndex]; }
    void setDebugRegister(int registerIndex, DWORD value) { *m_debugRegisterMap[registerIndex] = value; }

    BYTE& mutableReg8(RegisterIndex8 index) { return *m_byteRegisters[index]; }
    WORD& mutableReg16(RegisterIndex16 index) { return m_generalPurposeRegister[index].lowWORD; }
    DWORD& mutableReg32(RegisterIndex32 index) { return m_generalPurposeRegister[index].fullDWORD; }

    DWORD getEAX() const { return readRegister<DWORD>(RegisterEAX); }
    DWORD getEBX() const { return readRegister<DWORD>(RegisterEBX); }
    DWORD getECX() const { return readRegister<DWORD>(RegisterECX); }
    DWORD getEDX() const { return readRegister<DWORD>(RegisterEDX); }
    DWORD getESI() const { return readRegister<DWORD>(RegisterESI); }
    DWORD getEDI() const { return readRegister<DWORD>(RegisterEDI); }
    DWORD getESP() const { return readRegister<DWORD>(RegisterESP); }
    DWORD getEBP() const { return readRegister<DWORD>(RegisterEBP); }

    WORD getAX() const { return readRegister<WORD>(RegisterAX); }
    WORD getBX() const { return readRegister<WORD>(RegisterBX); }
    WORD getCX() const { return readRegister<WORD>(RegisterCX); }
    WORD getDX() const { return readRegister<WORD>(RegisterDX); }
    WORD getSI() const { return readRegister<WORD>(RegisterSI); }
    WORD getDI() const { return readRegister<WORD>(RegisterDI); }
    WORD getSP() const { return readRegister<WORD>(RegisterSP); }
    WORD getBP() const { return readRegister<WORD>(RegisterBP); }

    BYTE getAL() const { return readRegister<BYTE>(RegisterAL); }
    BYTE getBL() const { return readRegister<BYTE>(RegisterBL); }
    BYTE getCL() const { return readRegister<BYTE>(RegisterCL); }
    BYTE getDL() const { return readRegister<BYTE>(RegisterDL); }
    BYTE getAH() const { return readRegister<BYTE>(RegisterAH); }
    BYTE getBH() const { return readRegister<BYTE>(RegisterBH); }
    BYTE getCH() const { return readRegister<BYTE>(RegisterCH); }
    BYTE getDH() const { return readRegister<BYTE>(RegisterDH); }

    void setAL(BYTE value) { writeRegister<BYTE>(RegisterAL, value); }
    void setBL(BYTE value) { writeRegister<BYTE>(RegisterBL, value); }
    void setCL(BYTE value) { writeRegister<BYTE>(RegisterCL, value); }
    void setDL(BYTE value) { writeRegister<BYTE>(RegisterDL, value); }
    void setAH(BYTE value) { writeRegister<BYTE>(RegisterAH, value); }
    void setBH(BYTE value) { writeRegister<BYTE>(RegisterBH, value); }
    void setCH(BYTE value) { writeRegister<BYTE>(RegisterCH, value); }
    void setDH(BYTE value) { writeRegister<BYTE>(RegisterDH, value); }

    void setAX(WORD value) { writeRegister<WORD>(RegisterAX, value); }
    void setBX(WORD value) { writeRegister<WORD>(RegisterBX, value); }
    void setCX(WORD value) { writeRegister<WORD>(RegisterCX, value); }
    void setDX(WORD value) { writeRegister<WORD>(RegisterDX, value); }
    void setSP(WORD value) { writeRegister<WORD>(RegisterSP, value); }
    void setBP(WORD value) { writeRegister<WORD>(RegisterBP, value); }
    void setSI(WORD value) { writeRegister<WORD>(RegisterSI, value); }
    void setDI(WORD value) { writeRegister<WORD>(RegisterDI, value); }

    void setEAX(DWORD value) { writeRegister<DWORD>(RegisterEAX, value); }
    void setEBX(DWORD value) { writeRegister<DWORD>(RegisterEBX, value); }
    void setECX(DWORD value) { writeRegister<DWORD>(RegisterECX, value); }
    void setEDX(DWORD value) { writeRegister<DWORD>(RegisterEDX, value); }
    void setESP(DWORD value) { writeRegister<DWORD>(RegisterESP, value); }
    void setEBP(DWORD value) { writeRegister<DWORD>(RegisterEBP, value); }
    void setESI(DWORD value) { writeRegister<DWORD>(RegisterESI, value); }
    void setEDI(DWORD value) { writeRegister<DWORD>(RegisterEDI, value); }

    DWORD getCR0() const { return m_CR0; }
    DWORD getCR2() const { return m_CR2; }
    DWORD getCR3() const { return m_CR3; }
    DWORD getCR4() const { return m_CR4; }

    DWORD getDR0() const { return m_DR0; }
    DWORD getDR1() const { return m_DR1; }
    DWORD getDR2() const { return m_DR2; }
    DWORD getDR3() const { return m_DR3; }
    DWORD getDR4() const { return m_DR4; }
    DWORD getDR5() const { return m_DR5; }
    DWORD getDR6() const { return m_DR6; }
    DWORD getDR7() const { return m_DR7; }

    // Base CS:EIP is the start address of the currently executing instruction
    WORD getBaseCS() const { return m_baseCS; }
    WORD getBaseIP() const { return m_baseEIP & 0xFFFF; }
    DWORD getBaseEIP() const { return m_baseEIP; }

    DWORD currentStackPointer() const
    {
        if (s32())
            return getESP();
        return getSP();
    }
    DWORD currentBasePointer() const
    {
        if (s32())
            return getEBP();
        return getBP();
    }
    void setCurrentStackPointer(DWORD value)
    {
        if (s32())
            setESP(value);
        else
            setSP(value);
    }
    void setCurrentBasePointer(DWORD value)
    {
        if (s32())
            setEBP(value);
        else
            setBP(value);
    }
    void adjustStackPointer(int delta)
    {
        setCurrentStackPointer(currentStackPointer() + delta);
    }
    DWORD currentInstructionPointer() const
    {
        return x32() ? getEIP() : getIP();
    }
    DWORD currentBaseInstructionPointer() const
    {
        return x32() ? getBaseEIP() : getBaseIP();
    }
    void adjustInstructionPointer(int delta)
    {
        m_EIP += delta;
    }

    void farReturn(WORD stackAdjustment = 0);
    void realModeFarReturn(WORD stackAdjustment);
    void protectedFarReturn(WORD stackAdjustment);
    void protectedIRET(TransactionalPopper&, LogicalAddress);
    void clearSegmentRegisterAfterReturnIfNeeded(SegmentRegisterIndex, JumpType);

    void realModeFarJump(LogicalAddress, JumpType);
    void protectedModeFarJump(LogicalAddress, JumpType, Gate* = nullptr);
    void farJump(LogicalAddress, JumpType, Gate* = nullptr);
    void jumpRelative8(SIGNED_BYTE displacement);
    void jumpRelative16(SIGNED_WORD displacement);
    void jumpRelative32(SIGNED_DWORD displacement);
    void jumpAbsolute16(WORD offset);
    void jumpAbsolute32(DWORD offset);

    void decodeNext();
    void execute(Instruction&);

    void executeOneInstruction();

    // CPU main loop - will fetch & decode until stopped
    void mainLoop();
    bool mainLoopSlowStuff();

    // CPU main loop when halted (HLT) - will do nothing until an IRQ is raised
    void haltedLoop();

    void push32(DWORD value);
    DWORD pop32();
    void push16(WORD value);
    WORD pop16();

    template<typename T> T pop();
    template<typename T> void push(T);

    void pushValueWithSize(DWORD value, ValueSize size) { if (size == WordSize) push16(value); else push32(value); }
    void pushOperandSizedValue(DWORD value) { if (o16()) push16(value); else push32(value); }
    DWORD popOperandSizedValue() { return o16() ? pop16() : pop32(); }

    void pushSegmentRegisterValue(WORD);

    Debugger& debugger() { return *m_debugger; }

    template<typename T> T in(WORD port);
    template<typename T> void out(WORD port, T data);

    BYTE in8(WORD port);
    WORD in16(WORD port);
    DWORD in32(WORD port);
    void out8(WORD port, BYTE value);
    void out16(WORD port, WORD value);
    void out32(WORD port, DWORD value);

    const BYTE* memoryPointer(LinearAddress);
    const BYTE* memoryPointer(LogicalAddress);
    const BYTE* memoryPointer(SegmentRegisterIndex, DWORD offset);
    const BYTE* memoryPointer(const SegmentDescriptor&, DWORD offset);

    DWORD getEFlags() const;
    WORD getFlags() const;
    void setEFlags(DWORD flags);
    void setFlags(WORD flags);
    void setEFlagsRespectfully(DWORD flags, BYTE effectiveCPL);

    bool evaluate(BYTE) const;

    template<typename T> void updateFlags(T);
    void updateFlags32(DWORD value);
    void updateFlags16(WORD value);
    void updateFlags8(BYTE value);
    template<typename T> void mathFlags(typename TypeDoubler<T>::type result, T dest, T src);
    template<typename T> void cmpFlags(typename TypeDoubler<T>::type result, T dest, T src);

    void adjustFlag(QWORD result, DWORD src, DWORD dest)
    {
        setAF((((result ^ (src ^ dest)) & 0x10) >> 4) & 1);
    }

    template<typename T> T readRegister(int registerIndex) const;
    template<typename T> void writeRegister(int registerIndex, T value);

    DWORD readRegisterForAddressSize(int registerIndex);
    void writeRegisterForAddressSize(int registerIndex, DWORD);
    void stepRegisterForAddressSize(int registerIndex, DWORD stepSize);
    bool decrementCXForAddressSize();

    template<typename T> LogicalAddress readLogicalAddress(SegmentRegisterIndex, DWORD offset);

    template<typename T> bool validatePhysicalAddress(PhysicalAddress, MemoryAccessType);
    template<typename T> void validateAddress(const SegmentDescriptor&, DWORD offset, MemoryAccessType);
    template<typename T> void validateAddress(SegmentRegisterIndex, DWORD offset, MemoryAccessType);
    template<typename T> T readPhysicalMemory(PhysicalAddress);
    template<typename T> void writePhysicalMemory(PhysicalAddress, T);
    const BYTE* pointerToPhysicalMemory(PhysicalAddress);
    template<typename T> T readMemory(LinearAddress address, MemoryAccessType accessType = MemoryAccessType::Read);
    template<typename T> T readMemory(const SegmentDescriptor&, DWORD offset, MemoryAccessType accessType = MemoryAccessType::Read);
    template<typename T> T readMemory(SegmentRegisterIndex, DWORD offset, MemoryAccessType accessType = MemoryAccessType::Read);
    template<typename T> void writeMemory(LinearAddress, T);
    template<typename T> void writeMemory(const SegmentDescriptor&, DWORD offset, T);
    template<typename T> void writeMemory(SegmentRegisterIndex, DWORD offset, T);

    PhysicalAddress translateAddress(LinearAddress, MemoryAccessType);
    void snoop(LinearAddress, MemoryAccessType);
    void snoop(SegmentRegisterIndex, DWORD offset, MemoryAccessType);

    template<typename T> void validateIOAccess(WORD port);

    BYTE readMemory8(LinearAddress);
    BYTE readMemory8(SegmentRegisterIndex, DWORD offset);
    WORD readMemory16(LinearAddress);
    WORD readMemory16(SegmentRegisterIndex, DWORD offset);
    DWORD readMemory32(LinearAddress);
    DWORD readMemory32(SegmentRegisterIndex, DWORD offset);
    void writeMemory8(LinearAddress, BYTE);
    void writeMemory8(SegmentRegisterIndex, DWORD offset, BYTE data);
    void writeMemory16(LinearAddress, WORD);
    void writeMemory16(SegmentRegisterIndex, DWORD offset, WORD data);
    void writeMemory32(LinearAddress, DWORD);
    void writeMemory32(SegmentRegisterIndex, DWORD offset, DWORD data);

    enum State { Dead, Alive, Halted };
    State state() const { return m_state; }
    void setState(State s) { m_state = s; }

    SegmentDescriptor& cachedDescriptor(SegmentRegisterIndex index) { return m_descriptor[(int)index]; }
    const SegmentDescriptor& cachedDescriptor(SegmentRegisterIndex index) const { return m_descriptor[(int)index]; }

    // Dumps registers, flags & stack
    void dumpAll();
    void dumpStack(ValueSize, unsigned count);
    void dumpWatches();

    void dumpIVT();
    void dumpIDT();
    void dumpLDT();
    void dumpGDT();

    void dumpMemory(LogicalAddress, int rows);
    void dumpFlatMemory(DWORD address);
    void dumpRawMemory(BYTE*);
    unsigned dumpDisassembled(LogicalAddress, unsigned count = 1);

    void dumpMemory(SegmentDescriptor&, DWORD offset, int rows);
    unsigned dumpDisassembled(SegmentDescriptor&, DWORD offset, unsigned count = 1);
    unsigned dumpDisassembledInternal(SegmentDescriptor&, DWORD offset);

    void dumpTSS(const TSS&);

#ifdef CT_TRACE
    // Dumps registers (used by --trace)
    void dumpTrace();
#endif

    QVector<WatchedAddress>& watches() { return m_watches; }

    // Current execution mode (16 or 32 bit)
    bool x16() const { return !x32(); }
    bool x32() const { return cachedDescriptor(SegmentRegisterIndex::CS).D(); }

    bool a16() const { return !m_effectiveAddressSize32; }
    bool a32() const { return m_effectiveAddressSize32; }
    bool o16() const { return !m_effectiveOperandSize32; }
    bool o32() const { return m_effectiveOperandSize32; }

    bool s16() const { return !m_stackSize32; }
    bool s32() const { return m_stackSize32; }

    enum Command { ExitDebugger, EnterDebugger, HardReboot };
    void queueCommand(Command);

    static const char* registerName(CPU::RegisterIndex8) PURE;
    static const char* registerName(CPU::RegisterIndex16) PURE;
    static const char* registerName(CPU::RegisterIndex32) PURE;
    static const char* registerName(SegmentRegisterIndex) PURE;

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

    template<typename F> void doOnceOrRepeatedly(Instruction&, bool careAboutZF, F);
    template<typename T> void doLODS(Instruction&);
    template<typename T> void doSTOS(Instruction&);
    template<typename T> void doMOVS(Instruction&);
    template<typename T> void doINS(Instruction&);
    template<typename T> void doOUTS(Instruction&);
    template<typename T> void doCMPS(Instruction&);
    template<typename T> void doSCAS(Instruction&);

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

    template<typename T> void doMOV_moff_Areg(Instruction&);
    template<typename T> void doMOV_Areg_moff(Instruction&);

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

    template<typename T> void doNEG(Instruction&);
    template<typename T> void doNOT(Instruction&);
    void _NOT_RM8(Instruction&);
    void _NOT_RM16(Instruction&);
    void _NOT_RM32(Instruction&);
    void _NEG_RM8(Instruction&);
    void _NEG_RM16(Instruction&);
    void _NEG_RM32(Instruction&);

    template<typename T, class Accessor> void doDEC(Accessor);
    template<typename T, class Accessor> void doINC(Accessor);

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

    template<typename T> void doFarJump(Instruction&, JumpType);

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

    template<typename T> void doBOUND(Instruction&);
    void _BOUND(Instruction&);

    template<typename T> void doENTER(Instruction&);
    template<typename T> void doLEAVE();
    void _ENTER16(Instruction&);
    void _ENTER32(Instruction&);
    void _LEAVE16(Instruction&);
    void _LEAVE32(Instruction&);

    template<typename T> void doPUSHA();
    template<typename T> void doPOPA();
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

    template<typename T> void doLxS(Instruction&, SegmentRegisterIndex);
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

    void _LOCK(Instruction&);

    void _UD0(Instruction&);
    void _UD1(Instruction&);
    void _UD2(Instruction&);

    void handleRepeatOpcode(Instruction&, bool conditionForZF);

private:
    friend class Instruction;
    friend class InstructionExecutionContext;

    template<typename T> T readInstructionStream();
    BYTE readInstruction8() override;
    WORD readInstruction16() override;
    DWORD readInstruction32() override;

    void initWatches();
    void hardReboot();

    void updateDefaultSizes();
    void updateStackSize();
    void updateCodeSegmentCache();
    void makeNextInstructionUninterruptible();

    void didTouchMemory(PhysicalAddress);

    PhysicalAddress translateAddressSlowCase(LinearAddress, MemoryAccessType);

    template<typename T> T doSAR(T, unsigned steps);
    template<typename T> T doRCL(T, unsigned steps);
    template<typename T> T doRCR(T, unsigned steps);

    template<typename T> T doSHL(T, unsigned steps);
    template<typename T> T doSHR(T, unsigned steps);

    template<typename T> T doSHLD(T, T, unsigned steps);
    template<typename T> T doSHRD(T, T, unsigned steps);

    template<typename T> T doROL(T, unsigned steps);
    template<typename T> T doROR(T, unsigned steps);

    template<typename T> T doXOR(T, T);
    template<typename T> T doOR(T, T);
    template<typename T> T doAND(T, T);

    template<typename T> T doBT(T, int bitIndex);
    template<typename T> T doBTR(T, int bitIndex);
    template<typename T> T doBTC(T, int bitIndex);
    template<typename T> T doBTS(T, int bitIndex);
    template<typename T> T doBSF(T);
    template<typename T> T doBSR(T);

    template<typename T> QWORD doADD(T, T);
    template<typename T> QWORD doADC(T, T);
    template<typename T> QWORD doSUB(T, T);
    template<typename T> QWORD doSBB(T, T);
    template<typename T> void doIMUL(T f1, T f2, T& resultHigh, T& resultLow);
    template<typename T> void doMUL(T f1, T f2, T& resultHigh, T& resultLow);
    template<typename T> void doDIV(T dividendHigh, T dividendLow, T divisor, T& quotient, T& remainder);
    template<typename T> void doIDIV(T dividendHigh, T dividendLow, T divisor, T& quotient, T& remainder);

    void saveBaseAddress()
    {
        m_baseCS = getCS();
        m_baseEIP = getEIP();
    }

    void setLDT(WORD segment);
    void taskSwitch(WORD task, JumpType);
    void taskSwitch(TSSDescriptor&, JumpType);
    TSS currentTSS();

    void writeToGDT(Descriptor&);

    void dumpSelector(const char* prefix, SegmentRegisterIndex);
    void writeSegmentRegister(SegmentRegisterIndex, WORD selector);
    void validateSegmentLoad(SegmentRegisterIndex, WORD selector, const Descriptor&);

    SegmentDescriptor m_descriptor[6];

    PartAddressableRegister m_generalPurposeRegister[8];
    BYTE* m_byteRegisters[8];

    DWORD m_EIP { 0 };

    WORD CS, DS, ES, SS, FS, GS;
    mutable bool CF, PF, AF, ZF, SF, OF;
    bool DF, IF, TF;

    unsigned int IOPL;
    bool NT;
    bool RF;
    bool VM;
    bool AC;
    bool VIF;
    bool VIP;
    bool ID;

    DescriptorTableRegister m_GDTR { "GDT" };
    DescriptorTableRegister m_IDTR { "IDT" };
    DescriptorTableRegister m_LDTR { "LDT" };

    DWORD m_CR0 { 0 };
    DWORD m_CR2 { 0 };
    DWORD m_CR3 { 0 };
    DWORD m_CR4 { 0 };

    DWORD m_DR0, m_DR1, m_DR2, m_DR3, m_DR4, m_DR5, m_DR6, m_DR7;

    struct {
        WORD selector { 0 };
        LinearAddress base { 0 };
        WORD limit { 0 };
        bool is32Bit { false };
    } TR;

    State m_state { Dead };

    // Actual CS:EIP (when we started fetching the instruction)
    WORD m_baseCS { 0 };
    DWORD m_baseEIP { 0 };

    SegmentRegisterIndex m_segmentPrefix { SegmentRegisterIndex::None };

    DWORD m_baseMemorySize { 0 };
    DWORD m_extendedMemorySize { 0 };

    std::set<LogicalAddress> m_breakpoints;

    bool m_a20Enabled { false };
    bool m_nextInstructionIsUninterruptible { false };

    OwnPtr<Debugger> m_debugger;

    // One MemoryProvider* per 'memoryProviderBlockSize' bytes for the first MB of memory.
    static const size_t memoryProviderBlockSize = 16384;
    MemoryProvider* m_memoryProviders[1048576 / memoryProviderBlockSize];

    BYTE* m_memory { nullptr };
    size_t m_memorySize { 0 };

    WORD* m_segmentMap[8];
    DWORD* m_controlRegisterMap[8];
    DWORD* m_debugRegisterMap[8];

    Machine& m_machine;

    bool m_addressSize32 { false };
    bool m_operandSize32 { false };
    bool m_effectiveAddressSize32 { false };
    bool m_effectiveOperandSize32 { false };
    bool m_stackSize32 { false };

    enum DebuggerRequest { NoDebuggerRequest, PleaseEnterDebugger, PleaseExitDebugger };

    std::atomic<bool> m_mainLoopNeedsSlowStuff { false };
    std::atomic<DebuggerRequest> m_debuggerRequest { NoDebuggerRequest };
    std::atomic<bool> m_shouldHardReboot { false };

    QVector<WatchedAddress> m_watches;

    bool m_isForAutotest { false };

    QWORD m_cycle { 0 };

    mutable DWORD m_dirtyFlags { 0 };
    QWORD m_lastResult { 0 };
    unsigned m_lastOpSize { ByteSize };
};

extern CPU* g_cpu;

#include "debug.h"

ALWAYS_INLINE bool CPU::evaluate(BYTE conditionCode) const
{
    ASSERT(conditionCode <= 0xF);

    switch (conditionCode) {
    case  0: return this->OF;                            // O
    case  1: return !this->OF;                           // NO
    case  2: return this->CF;                            // B, C, NAE
    case  3: return !this->CF;                           // NB, NC, AE
    case  4: return getZF();                             // E, Z
    case  5: return !getZF();                            // NE, NZ
    case  6: return (this->CF | getZF());                // BE, NA
    case  7: return !(this->CF | getZF());               // NBE, A
    case  8: return getSF();                             // S
    case  9: return !getSF();                            // NS
    case 10: return getPF();                             // P, PE
    case 11: return !getPF();                            // NP, PO
    case 12: return getSF() ^ this->OF;                  // L, NGE
    case 13: return !(getSF() ^ this->OF);               // NL, GE
    case 14: return (getSF() ^ this->OF) | getZF();      // LE, NG
    case 15: return !((getSF() ^ this->OF) | getZF());   // NLE, G
    }
    return 0;
}

ALWAYS_INLINE BYTE& Instruction::reg8()
{
#ifdef DEBUG_INSTRUCTION
    ASSERT(m_cpu);
#endif
    return *m_cpu->m_byteRegisters[registerIndex()];
}

ALWAYS_INLINE WORD& Instruction::reg16()
{
#ifdef DEBUG_INSTRUCTION
    ASSERT(m_cpu);
#endif
    return m_cpu->m_generalPurposeRegister[registerIndex()].lowWORD;
}

ALWAYS_INLINE WORD& Instruction::segreg()
{
#ifdef DEBUG_INSTRUCTION
    ASSERT(m_cpu);
    ASSERT(registerIndex() < 6);
#endif
    return *m_cpu->m_segmentMap[registerIndex()];
}

ALWAYS_INLINE DWORD& Instruction::reg32()
{
#ifdef DEBUG_INSTRUCTION
    ASSERT(m_cpu);
    ASSERT(m_cpu->o32());
#endif
    return m_cpu->m_generalPurposeRegister[registerIndex()].fullDWORD;
}

template<> ALWAYS_INLINE BYTE& Instruction::reg<BYTE>() { return reg8(); }
template<> ALWAYS_INLINE WORD& Instruction::reg<WORD>() { return reg16(); }
template<> ALWAYS_INLINE DWORD& Instruction::reg<DWORD>() { return reg32(); }

template<typename T>
inline void CPU::updateFlags(T result)
{
    switch (TypeTrivia<T>::bits) {
    case 8:
        updateFlags8(result);
        break;
    case 16:
        updateFlags16(result);
        break;
    case 32:
        updateFlags32(result);
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
ALWAYS_INLINE T CPU::readRegister(int registerIndex) const
{
    if (sizeof(T) == 1)
        return *m_byteRegisters[registerIndex];
    if (sizeof(T) == 2)
        return m_generalPurposeRegister[registerIndex].lowWORD;
    if (sizeof(T) == 4)
        return m_generalPurposeRegister[registerIndex].fullDWORD;
    ASSERT_NOT_REACHED();
}

template<typename T>
ALWAYS_INLINE void CPU::writeRegister(int registerIndex, T value)
{
    if (sizeof(T) == 1)
        *m_byteRegisters[registerIndex] = value;
    else if (sizeof(T) == 2)
        m_generalPurposeRegister[registerIndex].lowWORD = value;
    else if (sizeof(T) == 4)
        m_generalPurposeRegister[registerIndex].fullDWORD = value;
    else
        ASSERT_NOT_REACHED();
}

inline DWORD MemoryOrRegisterReference::offset()
{
    ASSERT(!isRegister());
    if (m_a32)
        return m_offset32;
    else
        return m_offset16;
}

template<typename T>
inline T MemoryOrRegisterReference::read()
{
    ASSERT(m_cpu);
    if (isRegister())
        return m_cpu->readRegister<T>(m_registerIndex);
    return m_cpu->readMemory<T>(segment(), offset());
}

template<typename T>
inline void MemoryOrRegisterReference::write(T data)
{
    ASSERT(m_cpu);
    if (isRegister()) {
        m_cpu->writeRegister<T>(m_registerIndex, data);
        return;
    }
    m_cpu->writeMemory<T>(segment(), offset(), data);
}

inline BYTE MemoryOrRegisterReference::read8() { return read<BYTE>(); }
inline WORD MemoryOrRegisterReference::read16() { return read<WORD>(); }
inline DWORD MemoryOrRegisterReference::read32() { ASSERT(m_cpu->o32()); return read<DWORD>(); }
inline void MemoryOrRegisterReference::write8(BYTE data) { return write(data); }
inline void MemoryOrRegisterReference::write16(WORD data) { return write(data); }
inline void MemoryOrRegisterReference::write32(DWORD data) { ASSERT(m_cpu->o32()); return write(data); }

template<typename T>
void CPU::mathFlags(typename TypeDoubler<T>::type result, T dest, T src)
{
    typedef typename TypeDoubler<T>::type DT;
    m_dirtyFlags |= Flag::PF | Flag::ZF | Flag::SF;
    m_lastResult = result;
    m_lastOpSize = TypeTrivia<T>::bits;

    setCF(result & ((DT)TypeTrivia<T>::mask << TypeTrivia<T>::bits));
    adjustFlag(result, dest, src);
}

template<typename T>
inline void CPU::cmpFlags(typename TypeDoubler<T>::type result, T dest, T src)
{
    mathFlags<T>(result, dest, src);
    setOF((((result ^ dest) & (src ^ dest)) >> (TypeTrivia<T>::bits - 1)) & 1);
}

ALWAYS_INLINE void Instruction::execute(CPU& cpu)
{
    m_cpu = &cpu;
    cpu.setSegmentPrefix(m_segmentPrefix);
    cpu.m_effectiveOperandSize32 = m_o32;
    cpu.m_effectiveAddressSize32 = m_a32;
    if (m_hasRM)
        m_modrm.resolve(cpu);
    (cpu.*m_impl)(*this);
}
