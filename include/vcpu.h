#ifndef VCPU_H
#define VCPU_H

#include "types.h"
#include "debug.h"

// MACROS AND CONVENIENCE METHODS
#define IS_VGA_MEMORY(address) ((address) >= 0xA0000 && (address) < 0xB0000)

inline DWORD vomit_toFlatAddress(WORD segment, WORD offset)
{
    return (segment << 4) + offset;
}

inline void vomit_write16ToPointer(WORD* pointer, WORD value)
{
#ifdef VOMIT_BIG_ENDIAN
    *pointer = V_BYTESWAP(value);
#else
    *pointer = value;
#endif
}

inline DWORD vomit_read32FromPointer(DWORD* pointer)
{
#ifdef VOMIT_BIG_ENDIAN
    return V_BYTESWAP(*pointer);
#else
    return *pointer;
#endif
}

inline void vomit_write32ToPointer(DWORD* pointer, DWORD value)
{
#ifdef VOMIT_BIG_ENDIAN
#error IMPLEMENT ME
#else
    *pointer = value;
#endif
}

inline WORD vomit_read16FromPointer(WORD* pointer)
{
#ifdef VOMIT_BIG_ENDIAN
    return V_BYTESWAP(*pointer);
#else
    return *pointer;
#endif
}

inline WORD vomit_signExtend(BYTE b)
{
    if (b & 0x80)
        return b | 0xFF00;
    else
        return b;
}

inline int vomit_modRMRegisterPart(int rmbyte)
{
    return (rmbyte >> 3) & 7;
}

/* Construct a 16-bit word from two 8-bit bytes */
#define MAKEWORD(l, m)	(((m) << 8) | (l))

#define LSW(d) ((d)&0xFFFF)
#define MSW(d) (((d)&0xFFFF0000)>>16)

#define LSB(w) ((w)&0xFF)
#define MSB(w) (((w)&0xFF00)>>8)

#define FLAT(s,o) (((s)<<4)+(o))

// VCPU MONSTROSITY

class VgaMemory;

class VCpu : public QObject
{
    Q_OBJECT
public:

    Q_PROPERTY(DWORD EAX READ getEAX WRITE setEAX)
    Q_PROPERTY(DWORD EBX READ getEBX WRITE setEBX)
    Q_PROPERTY(DWORD ECX READ getECX WRITE setECX)
    Q_PROPERTY(DWORD EDX READ getEDX WRITE setEDX)
    Q_PROPERTY(DWORD EBP READ getEBP WRITE setEBP)
    Q_PROPERTY(DWORD ESP READ getESP WRITE setESP)
    Q_PROPERTY(DWORD ESI READ getESI WRITE setESI)
    Q_PROPERTY(DWORD SDI READ getEDI WRITE setEDI)

    Q_PROPERTY(WORD AX READ getAX WRITE setAX)
    Q_PROPERTY(WORD BX READ getBX WRITE setBX)
    Q_PROPERTY(WORD CX READ getCX WRITE setCX)
    Q_PROPERTY(WORD DX READ getDX WRITE setDX)
    Q_PROPERTY(WORD BP READ getBP WRITE setBP)
    Q_PROPERTY(WORD SP READ getSP WRITE setSP)
    Q_PROPERTY(WORD SI READ getSI WRITE setSI)
    Q_PROPERTY(WORD DI READ getDI WRITE setDI)

    Q_PROPERTY(BYTE AL READ getAL WRITE setAL)
    Q_PROPERTY(BYTE BL READ getBL WRITE setBL)
    Q_PROPERTY(BYTE CL READ getCL WRITE setCL)
    Q_PROPERTY(BYTE DL READ getDL WRITE setDL)
    Q_PROPERTY(BYTE AH READ getAH WRITE setAH)
    Q_PROPERTY(BYTE BH READ getBH WRITE setBH)
    Q_PROPERTY(BYTE CH READ getCH WRITE setCH)
    Q_PROPERTY(BYTE DH READ getDH WRITE setDH)

    Q_PROPERTY(DWORD CS READ getCS WRITE setCS)
    Q_PROPERTY(DWORD DS READ getDS WRITE setDS)
    Q_PROPERTY(DWORD ES READ getES WRITE setES)
    Q_PROPERTY(DWORD SS READ getSS WRITE setSS)
    Q_PROPERTY(DWORD FS READ getFS WRITE setFS)
    Q_PROPERTY(DWORD GS READ getGS WRITE setGS)

    Q_PROPERTY(bool IF READ getIF WRITE setIF)
    Q_PROPERTY(bool OF READ getOF WRITE setOF)
    Q_PROPERTY(bool ZF READ getZF WRITE setZF)
    Q_PROPERTY(bool SF READ getSF WRITE setSF)
    Q_PROPERTY(bool CF READ getCF WRITE setCF)
    Q_PROPERTY(bool AF READ getAF WRITE setAF)
    Q_PROPERTY(bool TF READ getTF WRITE setTF)
    Q_PROPERTY(bool PF READ getPF WRITE setPF)

    Q_PROPERTY(DWORD EIP READ getEIP WRITE setEIP)
    Q_PROPERTY(WORD IP READ getIP WRITE setIP)

    VCpu(QObject* parent = 0);
    ~VCpu();

    enum {
        RegisterAL, RegisterCL, RegisterDL, RegisterBL,
        RegisterAH, RegisterCH, RegisterDH, RegisterBH
    };

    enum {
        RegisterAX, RegisterCX, RegisterDX, RegisterBX,
        RegisterSP, RegisterBP, RegisterSI, RegisterDI
    };

    enum {
        RegisterEAX, RegisterECX, RegisterEDX, RegisterEBX,
        RegisterESP, RegisterEBP, RegisterESI, RegisterEDI
    };

    enum {
        RegisterES, RegisterCS, RegisterSS, RegisterDS,
        RegisterFS, RegisterGS
    };

    union {
        struct {
            DWORD EAX, EBX, ECX, EDX;
            DWORD EBP, ESP, ESI, EDI;
            DWORD EIP;
        } D;
#ifdef VOMIT_BIG_ENDIAN
        struct {
            WORD __EAX_high_word, AX;
            WORD __EBX_high_word, BX;
            WORD __ECX_high_word, CX;
            WORD __EDX_high_word, DX;
            WORD __EBP_high_word, BP;
            WORD __ESP_high_word, SP;
            WORD __ESI_high_word, SI;
            WORD __EDI_high_word, DI;
            WORD __EIP_high_word, IP;
        } W;
        struct {
            WORD __EAX_high_word;
            BYTE AH, AL;
            WORD __EBX_high_word;
            BYTE BH, BL;
            WORD __ECX_high_word;
            BYTE CH, CL;
            WORD __EDX_high_word;
            BYTE DH, DL;
            DWORD EBP;
            DWORD ESP;
            DWORD ESI;
            DWORD EDI;
            DWORD EIP;
        } B;
#else
        struct {
            WORD AX, __EAX_high_word;
            WORD BX, __EBX_high_word;
            WORD CX, __ECX_high_word;
            WORD DX, __EDX_high_word;
            WORD BP, __EBP_high_word;
            WORD SP, __ESP_high_word;
            WORD SI, __ESI_high_word;
            WORD DI, __EDI_high_word;
            WORD IP, __EIP_high_word;
        } W;
        struct {
            BYTE AL, AH;
            WORD __EAX_high_word;
            BYTE BL, BH;
            WORD __EBX_high_word;
            BYTE CL, CH;
            WORD __ECX_high_word;
            BYTE DL, DH;
            WORD __EDX_high_word;
            DWORD EBP;
            DWORD ESP;
            DWORD ESI;
            DWORD EDI;
            DWORD EIP;
        } B;
#endif
    } regs;

    WORD CS, DS, ES, SS, FS, GS;

    WORD currentSegment() const { return *m_currentSegment; }
    bool hasSegmentPrefix() const { return m_currentSegment == &m_segmentPrefix; }

    void setSegmentPrefix(WORD segment)
    {
        m_segmentPrefix = segment;
        m_currentSegment = &m_segmentPrefix;
    }

    void resetSegmentPrefix() { m_currentSegment = &this->DS; }

    struct {
        DWORD base;
        WORD limit;
    } GDTR;

    struct {
        DWORD base;
        WORD limit;
    } IDTR;

    struct {
        WORD segment;
        DWORD base;
        WORD limit;
        // LDT's index in GDT
        int index;
    } LDTR;

    DWORD CR0, CR1, CR2, CR3, CR4, CR5, CR6, CR7;
    DWORD DR0, DR1, DR2, DR3, DR4, DR5, DR6, DR7;

    union {
        struct {
#ifdef VOMIT_BIG_ENDIAN
            WORD __EIP_high_word, IP;
#else
            WORD IP, __EIP_high_word;
#endif
        };
        DWORD EIP;
    };

    struct {
        WORD segment;
        DWORD base;
        WORD limit;
    } TR;

    BYTE opcode;
    BYTE rmbyte;
    BYTE subrmbyte;

    // Extended memory size in KiB (will be reported by CMOS)
    DWORD extendedMemorySize() const { return m_extendedMemorySize; }

    // Conventional memory size in KiB (will be reported by CMOS)
    DWORD baseMemorySize() const { return m_baseMemorySize; }

    // RAM
    BYTE* memory;

    // ID-to-Register maps
    DWORD* treg32[8];
    WORD* treg16[8];
    BYTE* treg8[8];
    WORD* tseg[8];

    void kill();

#ifdef VOMIT_DEBUG
    void attachDebugger();
    void detachDebugger();
    bool inDebugger() const;

    void debugger();
#else
    bool inDebugger() const { return false; }
#endif

    void jumpToInterruptHandler(int isr);
    void setInterruptHandler(BYTE isr, WORD segment, WORD offset);

    void GP(int code);

    void exception(int ec) { this->IP = getBaseIP(); jumpToInterruptHandler(ec); }

    void setIF(bool value) { this->IF = value; }
    void setCF(bool value) { this->CF = value; }
    void setDF(bool value) { this->DF = value; }
    void setSF(bool value) { this->SF = value; }
    void setAF(bool value) { this->AF = value; }
    void setTF(bool value) { this->TF = value; }
    void setOF(bool value) { this->OF = value; }
    void setPF(bool value) { this->PF = value; }
    void setZF(bool value) { this->ZF = value; }
    void setVIF(bool value) { this->VIF = value; }

    bool getIF() const { return this->IF; }
    bool getCF() const { return this->CF; }
    bool getDF() const { return this->DF; }
    bool getSF() const { return this->SF; }
    bool getAF() const { return this->AF; }
    bool getTF() const { return this->TF; }
    bool getOF() const { return this->OF; }
    bool getPF() const { return this->PF; }
    bool getZF() const { return this->ZF; }
    unsigned int getIOPL() const { return this->IOPL; }
    unsigned int getCPL() const { return this->CPL; }
    bool getVIP() const { return this->VIP; }
    bool getVIF() const { return this->VIF; }
    bool getVM() const { return this->VM; }
    bool getPE() const { return this->CR0 & 0x01; }
    bool getVME() const { return this->CR4 & 0x01; }
    bool getPVI() const { return this->CR4 & 0x02; }

    WORD getCS() const { return this->CS; }
    WORD getIP() const { return this->IP; }
    DWORD getEIP() const { return this->EIP; }

    WORD getDS() const { return this->DS; }
    WORD getES() const { return this->ES; }
    WORD getSS() const { return this->SS; }
    WORD getFS() const { return this->FS; }
    WORD getGS() const { return this->GS; }

    void setCS(WORD cs) { this->CS = cs; }
    void setDS(WORD ds) { this->DS = ds; }
    void setES(WORD es) { this->ES = es; }
    void setSS(WORD ss) { this->SS = ss; }
    void setFS(WORD fs) { this->FS = fs; }
    void setGS(WORD gs) { this->GS = gs; }

    void setIP(WORD ip) { this->IP = ip; }
    void setEIP(DWORD eip) { this->EIP = eip; }

    DWORD getEAX() const { return this->regs.D.EAX; }
    DWORD getEBX() const { return this->regs.D.EBX; }
    DWORD getECX() const { return this->regs.D.ECX; }
    DWORD getEDX() const { return this->regs.D.EDX; }
    DWORD getESI() const { return this->regs.D.ESI; }
    DWORD getEDI() const { return this->regs.D.EDI; }
    DWORD getESP() const { return this->regs.D.ESP; }
    DWORD getEBP() const { return this->regs.D.EBP; }

    WORD getAX() const { return this->regs.W.AX; }
    WORD getBX() const { return this->regs.W.BX; }
    WORD getCX() const { return this->regs.W.CX; }
    WORD getDX() const { return this->regs.W.DX; }
    WORD getSI() const { return this->regs.W.SI; }
    WORD getDI() const { return this->regs.W.DI; }
    WORD getSP() const { return this->regs.W.SP; }
    WORD getBP() const { return this->regs.W.BP; }

    BYTE getAL() const { return this->regs.B.AL; }
    BYTE getBL() const { return this->regs.B.BL; }
    BYTE getCL() const { return this->regs.B.CL; }
    BYTE getDL() const { return this->regs.B.DL; }
    BYTE getAH() const { return this->regs.B.AH; }
    BYTE getBH() const { return this->regs.B.BH; }
    BYTE getCH() const { return this->regs.B.CH; }
    BYTE getDH() const { return this->regs.B.DH; }

    void setAL(BYTE value) { this->regs.B.AL = value; }
    void setBL(BYTE value) { this->regs.B.BL = value; }
    void setCL(BYTE value) { this->regs.B.CL = value; }
    void setDL(BYTE value) { this->regs.B.DL = value; }
    void setAH(BYTE value) { this->regs.B.AH = value; }
    void setBH(BYTE value) { this->regs.B.BH = value; }
    void setCH(BYTE value) { this->regs.B.CH = value; }
    void setDH(BYTE value) { this->regs.B.DH = value; }

    void setAX(WORD value) { this->regs.W.AX = value; }
    void setBX(WORD value) { this->regs.W.BX = value; }
    void setCX(WORD value) { this->regs.W.CX = value; }
    void setDX(WORD value) { this->regs.W.DX = value; }
    void setSP(WORD value) { this->regs.W.SP = value; }
    void setBP(WORD value) { this->regs.W.BP = value; }
    void setSI(WORD value) { this->regs.W.SI = value; }
    void setDI(WORD value) { this->regs.W.DI = value; }

    void setEAX(DWORD value) { this->regs.D.EAX = value; }
    void setEBX(DWORD value) { this->regs.D.EBX = value; }
    void setECX(DWORD value) { this->regs.D.ECX = value; }
    void setEDX(DWORD value) { this->regs.D.EDX = value; }
    void setESP(DWORD value) { this->regs.D.ESP = value; }
    void setEBP(DWORD value) { this->regs.D.EBP = value; }
    void setESI(DWORD value) { this->regs.D.ESI = value; }
    void setEDI(DWORD value) { this->regs.D.EDI = value; }

    DWORD getCR0() const { return this->CR0; }
    DWORD getCR1() const { return this->CR1; }
    DWORD getCR2() const { return this->CR2; }
    DWORD getCR3() const { return this->CR3; }
    DWORD getCR4() const { return this->CR4; }
    DWORD getCR5() const { return this->CR5; }
    DWORD getCR6() const { return this->CR6; }
    DWORD getCR7() const { return this->CR7; }

    DWORD getDR0() const { return this->DR0; }
    DWORD getDR1() const { return this->DR1; }
    DWORD getDR2() const { return this->DR2; }
    DWORD getDR3() const { return this->DR3; }
    DWORD getDR4() const { return this->DR4; }
    DWORD getDR5() const { return this->DR5; }
    DWORD getDR6() const { return this->DR6; }
    DWORD getDR7() const { return this->DR7; }

    // Base CS:EIP is the start address of the currently executing instruction
    WORD getBaseCS() const { return m_baseCS; }
    WORD getBaseIP() const { return m_baseEIP & 0xFFFF; }
    WORD getBaseEIP() const { return m_baseEIP; }

    void jump32(WORD segment, DWORD offset);
    void jump16(WORD segment, WORD offset);
    void jumpRelative8(SIGNED_BYTE displacement);
    void jumpRelative16(SIGNED_WORD displacement);
    void jumpRelative32(SIGNED_DWORD displacement);
    void jumpAbsolute16(WORD offset);

    // Execute the next instruction at CS:IP (huge switch version)
    void decodeNext();

    // Execute the specified instruction
    void decode(BYTE op);

    // Execute the next instruction at CS:IP
    void exec();

    // CPU main loop - will fetch & decode until stopped
    void mainLoop();

    // CPU main loop when halted (HLT) - will do nothing until an IRQ is raised
    void haltedLoop();

#ifdef VOMIT_PREFETCH_QUEUE
    void flushFetchQueue();
    BYTE fetchOpcodeByte();
    WORD fetchOpcodeWord();
    DWORD fetchOpcodeDWord();
#else
    void flushFetchQueue() {}
    BYTE fetchOpcodeByte() { return m_codeMemory[this->IP++]; }
    inline WORD fetchOpcodeWord();
    inline DWORD fetchOpcodeDWord();
#endif

    void push32(DWORD value);
    DWORD pop32();

    void push(WORD value);
    WORD pop();

    /*!
        Writes an 8-bit value to an output port.
     */
    void out(WORD port, BYTE value);

    /*!
        Reads an 8-bit value from an input port.
     */
    BYTE in(WORD port);

    inline BYTE* memoryPointer(WORD segment, WORD offset) const;

    DWORD getEFlags() const;
    WORD getFlags() const;
    void setEFlags(DWORD flags);
    void setFlags(WORD flags);

    inline bool evaluate(BYTE) const;

    void updateFlags(WORD value, BYTE bits);
    void updateFlags32(DWORD value);
    void updateFlags16(WORD value);
    void updateFlags8(BYTE value);
    void mathFlags8(WORD result, BYTE dest, BYTE src);
    void mathFlags16(DWORD result, WORD dest, WORD src);
    void mathFlags32(QWORD result, DWORD dest, DWORD src);
    void cmpFlags8(DWORD result, BYTE dest, BYTE src);
    void cmpFlags16(DWORD result, WORD dest, WORD src);
    void cmpFlags32(QWORD result, DWORD dest, DWORD src);

    void adjustFlag32(DWORD result, WORD dest, WORD src);

    // These are faster than readMemory*() but will not access VGA memory, etc.
    inline BYTE readUnmappedMemory8(DWORD address) const;
    inline WORD readUnmappedMemory16(DWORD address) const;
    inline void writeUnmappedMemory8(DWORD address, BYTE data);
    inline void writeUnmappedMemory16(DWORD address, WORD data);

    inline BYTE readMemory8(DWORD address) const;
    inline BYTE readMemory8(WORD segment, WORD offset) const;
    inline WORD readMemory16(DWORD address) const;
    inline WORD readMemory16(WORD segment, WORD offset) const;
    DWORD readMemory32(DWORD address) const;
    inline DWORD readMemory32(WORD segment, WORD offset) const;
    inline void writeMemory8(DWORD address, BYTE data);
    inline void writeMemory8(WORD segment, WORD offset, BYTE data);
    inline void writeMemory16(DWORD address, WORD data);
    inline void writeMemory16(WORD segment, WORD offset, WORD data);
    void writeMemory32(DWORD address, DWORD data);
    inline void writeMemory32(WORD segment, WORD offset, DWORD data);

    BYTE readModRM8(BYTE rmbyte);
    WORD readModRM16(BYTE rmbyte);
    DWORD readModRM32(BYTE rmbyte);
    void writeModRM8(BYTE rmbyte, BYTE value);
    void writeModRM16(BYTE rmbyte, WORD value);
    void writeModRM32(BYTE rmbyte, DWORD value);

    /*!
        Writes an 8-bit value back to the most recently resolved ModR/M location.
     */
    void updateModRM8(BYTE value);

    /*!
        Writes a 16-bit value back to the most recently resolved ModR/M location.
     */
    void updateModRM16(WORD value);

    /*!
        Writes a 32-bit value back to the most recently resolved ModR/M location.
     */
    void updateModRM32(DWORD value);

    void* resolveModRM8(BYTE rmbyte);
    void* resolveModRM16(BYTE rmbyte);
    void* resolveModRM32(BYTE rmbyte);

    DWORD evaluateSIB(BYTE sib);

    VgaMemory* vgaMemory;

    enum Mode { RealMode, ProtectedMode };
    Mode mode() const { return m_mode; }
    void setMode(Mode m) { m_mode = m; }

    enum State { Dead, Alive, Halted };
    State state() const { return m_state; }
    void setState(State s) { m_state = s; }

    // TODO: make private
    inline BYTE* codeMemory() const;

    /* TODO: actual PIT implementation.. */
    inline bool tick();

    // Dumps some basic information about this CPU
    void dump() const;

    // Dumps registers, flags & stack
    void dumpAll() const;

    // Dumps all ISR handler pointers (0000:0000 - 0000:03FF)
    void dumpIVT() const;

    void dumpMemory(WORD segment, DWORD offset, int rows) const;

    int dumpDisassembled(WORD segment, DWORD offset) const;

#ifdef VOMIT_TRACE
    // Dumps registers (used by --trace)
    void dumpTrace() const;
#endif

#ifdef VOMIT_DETECT_UNINITIALIZED_ACCESS
    void markDirty(DWORD address) { m_dirtMap[address] = true; }
#endif

    bool m_addressSize32;
    bool m_operationSize32;

    bool a16() const { return !m_addressSize32; }
    bool a32() const { return m_addressSize32; }
    bool o16() const { return !m_operationSize32; }
    bool o32() const { return m_operationSize32; }

    void nextSI(int size) { this->regs.W.SI += (getDF() ? -size : size); }
    void nextDI(int size) { this->regs.W.DI += (getDF() ? -size : size); }
    void nextESI(int size) { this->regs.D.ESI += (getDF() ? -size : size); }
    void nextEDI(int size) { this->regs.D.EDI += (getDF() ? -size : size); }

private:
    void* resolveModRM8_internal(BYTE rmbyte);
    void* resolveModRM16_internal(BYTE rmbyte);
    void* resolveModRM32_internal(BYTE rmbyte);

    void saveBaseAddress()
    {
        m_baseCS = getCS();
        m_baseEIP = getEIP();
    }

    DWORD m_instructionsPerTick;

    // This points to the base of CS for fast opcode fetches.
    BYTE* m_codeMemory;

    bool CF, DF, TF, PF, AF, ZF, SF, IF, OF;

    unsigned int CPL;
    unsigned int IOPL;
    bool NT;
    bool RF;
    bool VM;
    bool AC;
    bool VIF;
    bool VIP;
    bool ID;

    State m_state;

    Mode m_mode;

#ifdef VOMIT_DEBUG
    bool m_inDebugger;
    bool m_debugOneStep;
#endif

    // Cycle counter. May wrap arbitrarily.
    DWORD m_pitCountdown;

    // Actual CS:EIP (when we started fetching the instruction)
    WORD m_baseCS;
    DWORD m_baseEIP;

    WORD* m_currentSegment;
    WORD m_segmentPrefix;

#ifdef VOMIT_DETECT_UNINITIALIZED_ACCESS
    bool* m_dirtMap;
#endif

    mutable void* m_lastModRMPointer;
    mutable WORD m_lastModRMSegment;
    mutable DWORD m_lastModRMOffset;

    // FIXME: Don't befriend this... thing.
    friend void unspeakable_abomination();
    DWORD m_baseMemorySize;
    DWORD m_extendedMemorySize;
};

extern VCpu* g_cpu;

WORD cpu_add8(BYTE, BYTE);
WORD cpu_sub8(BYTE, BYTE);
WORD cpu_mul8(BYTE, BYTE);
WORD cpu_div8(BYTE, BYTE);
SIGNED_WORD cpu_imul8(SIGNED_BYTE, SIGNED_BYTE);

DWORD cpu_add16(VCpu*, WORD, WORD);
QWORD cpu_add32(VCpu*, DWORD, DWORD);
DWORD cpu_sub16(VCpu*, WORD, WORD);
QWORD cpu_sub32(VCpu*, DWORD, DWORD);
DWORD cpu_mul16(VCpu*, WORD, WORD);
DWORD cpu_div16(VCpu*, WORD, WORD);
SIGNED_DWORD cpu_imul16(VCpu*, SIGNED_WORD, SIGNED_WORD);

BYTE cpu_or8(VCpu*, BYTE, BYTE);
BYTE cpu_and8(VCpu*, BYTE, BYTE);
BYTE cpu_xor8(VCpu*, BYTE, BYTE);
WORD cpu_or16(VCpu*, WORD, WORD);
WORD cpu_and16(VCpu*, WORD, WORD);
WORD cpu_xor16(VCpu*, WORD, WORD);
DWORD cpu_or32(VCpu*, DWORD, DWORD);
DWORD cpu_xor32(VCpu*, DWORD, DWORD);
DWORD cpu_and32(VCpu*, DWORD, DWORD);

DWORD cpu_shl(VCpu*, WORD, BYTE, BYTE);
DWORD cpu_shr(VCpu*, WORD, BYTE, BYTE);
DWORD cpu_sar(VCpu*, WORD, BYTE, BYTE);
DWORD cpu_rcl(VCpu*, WORD, BYTE, BYTE);
DWORD cpu_rcr(VCpu*, WORD, BYTE, BYTE);
DWORD cpu_rol(VCpu*, WORD, BYTE, BYTE);
DWORD cpu_ror(VCpu*, WORD, BYTE, BYTE);

void _UNSUPP(VCpu*);
void _ESCAPE(VCpu*);

void _NOP(VCpu*);
void _HLT(VCpu*);
void _INT_imm8(VCpu*);
void _INT3(VCpu*);
void _INTO(VCpu*);
void _IRET(VCpu*);

void _AAA(VCpu*);
void _AAM(VCpu*);
void _AAD(VCpu*);
void _AAS(VCpu*);

void _DAA(VCpu*);
void _DAS(VCpu*);

void _STC(VCpu*);
void _STD(VCpu*);
void _STI(VCpu*);
void _CLC(VCpu*);
void _CLD(VCpu*);
void _CLI(VCpu*);
void _CMC(VCpu*);

void _CBW(VCpu*);
void _CWD(VCpu*);
void _CWDE(VCpu*);
void _CDQ(VCpu*);

void _XLAT(VCpu*);

void _CS(VCpu*);
void _DS(VCpu*);
void _ES(VCpu*);
void _SS(VCpu*);

void _SALC(VCpu*);

void _JMP_imm32(VCpu*);
void _JMP_imm16(VCpu*);
void _JMP_imm16_imm16(VCpu*);
void _JMP_short_imm8(VCpu*);
void _Jcc_imm8(VCpu*);
void _JCXZ_imm8(VCpu*);
void _JECXZ_imm8(VCpu*);

void _JO_imm8(VCpu*);
void _JNO_imm8(VCpu*);
void _JC_imm8(VCpu*);
void _JNC_imm8(VCpu*);
void _JZ_imm8(VCpu*);
void _JNZ_imm8(VCpu*);
void _JNA_imm8(VCpu*);
void _JA_imm8(VCpu*);
void _JS_imm8(VCpu*);
void _JNS_imm8(VCpu*);
void _JP_imm8(VCpu*);
void _JNP_imm8(VCpu*);
void _JL_imm8(VCpu*);
void _JNL_imm8(VCpu*);
void _JNG_imm8(VCpu*);
void _JG_imm8(VCpu*);

void _JO_NEAR_imm(VCpu*);
void _JNO_NEAR_imm(VCpu*);
void _JC_NEAR_imm(VCpu*);
void _JNC_NEAR_imm(VCpu*);
void _JZ_NEAR_imm(VCpu*);
void _JNZ_NEAR_imm(VCpu*);
void _JNA_NEAR_imm(VCpu*);
void _JA_NEAR_imm(VCpu*);
void _JS_NEAR_imm(VCpu*);
void _JNS_NEAR_imm(VCpu*);
void _JP_NEAR_imm(VCpu*);
void _JNP_NEAR_imm(VCpu*);
void _JL_NEAR_imm(VCpu*);
void _JNL_NEAR_imm(VCpu*);
void _JNG_NEAR_imm(VCpu*);
void _JG_NEAR_imm(VCpu*);

void _CALL_imm16(VCpu*);
void _CALL_imm32(VCpu*);
void _RET(VCpu*);
void _RET_imm16(VCpu*);
void _RETF(VCpu*);
void _RETF_imm16(VCpu*);

void _LOOP_imm8(VCpu*);
void _LOOPE_imm8(VCpu*);
void _LOOPNE_imm8(VCpu*);

void _REP(VCpu*);
void _REPNE(VCpu*);

void _XCHG_AX_reg16(VCpu*);
void _XCHG_EAX_reg32(VCpu*);
void _XCHG_reg8_RM8(VCpu*);
void _XCHG_reg16_RM16(VCpu*);
void _XCHG_reg32_RM32(VCpu*);

void _CMPSB(VCpu*);
void _CMPSW(VCpu*);
void _CMPSD(VCpu*);
void _LODSB(VCpu*);
void _LODSW(VCpu*);
void _LODSD(VCpu*);
void _SCASB(VCpu*);
void _SCASW(VCpu*);
void _SCASD(VCpu*);
void _STOSB(VCpu*);
void _STOSW(VCpu*);
void _STOSD(VCpu*);
void _MOVSB(VCpu*);
void _MOVSW(VCpu*);
void _MOVSD(VCpu*);

void _LEA_reg16_mem16(VCpu*);
void _LEA_reg32_mem32(VCpu*);

void _LDS_reg16_mem16(VCpu*);
void _LDS_reg32_mem32(VCpu*);
void _LES_reg16_mem16(VCpu*);
void _LES_reg32_mem32(VCpu*);

void _MOV_AL_imm8(VCpu*);
void _MOV_BL_imm8(VCpu*);
void _MOV_CL_imm8(VCpu*);
void _MOV_DL_imm8(VCpu*);
void _MOV_AH_imm8(VCpu*);
void _MOV_BH_imm8(VCpu*);
void _MOV_CH_imm8(VCpu*);
void _MOV_DH_imm8(VCpu*);

void _MOV_AX_imm16(VCpu*);
void _MOV_BX_imm16(VCpu*);
void _MOV_CX_imm16(VCpu*);
void _MOV_DX_imm16(VCpu*);
void _MOV_BP_imm16(VCpu*);
void _MOV_SP_imm16(VCpu*);
void _MOV_SI_imm16(VCpu*);
void _MOV_DI_imm16(VCpu*);

void _MOV_seg_RM16(VCpu*);
void _MOV_RM16_seg(VCpu*);
void _MOV_RM32_seg(VCpu*);
void _MOV_AL_moff8(VCpu*);
void _MOV_AX_moff16(VCpu*);
void _MOV_EAX_moff32(VCpu*);
void _MOV_moff8_AL(VCpu*);
void _MOV_moff16_AX(VCpu*);
void _MOV_reg8_RM8(VCpu*);
void _MOV_reg16_RM16(VCpu*);
void _MOV_RM8_reg8(VCpu*);
void _MOV_RM16_reg16(VCpu*);
void _MOV_RM8_imm8(VCpu*);
void _MOV_RM16_imm16(VCpu*);
void _MOV_RM32_imm32(VCpu*);

void _XOR_RM8_reg8(VCpu*);
void _XOR_RM16_reg16(VCpu*);
void _XOR_reg8_RM8(VCpu*);
void _XOR_reg16_RM16(VCpu*);
void _XOR_reg32_RM32(VCpu*);
void _XOR_RM8_imm8(VCpu*);
void _XOR_RM16_imm16(VCpu*);
void _XOR_RM16_imm8(VCpu*);
void _XOR_AL_imm8(VCpu*);
void _XOR_AX_imm16(VCpu*);
void _XOR_EAX_imm32(VCpu*);

void _OR_RM8_reg8(VCpu*);
void _OR_RM16_reg16(VCpu*);
void _OR_RM32_reg32(VCpu*);
void _OR_reg8_RM8(VCpu*);
void _OR_reg16_RM16(VCpu*);
void _OR_reg32_RM32(VCpu*);
void _OR_RM8_imm8(VCpu*);
void _OR_RM16_imm16(VCpu*);
void _OR_RM16_imm8(VCpu*);
void _OR_EAX_imm32(VCpu*);
void _OR_AX_imm16(VCpu*);
void _OR_AL_imm8(VCpu*);

void _AND_RM8_reg8(VCpu*);
void _AND_RM16_reg16(VCpu*);
void _AND_reg8_RM8(VCpu*);
void _AND_reg16_RM16(VCpu*);
void _AND_RM8_imm8(VCpu*);
void _AND_RM16_imm16(VCpu*);
void _AND_RM16_imm8(VCpu*);
void _AND_AL_imm8(VCpu*);
void _AND_AX_imm16(VCpu*);
void _AND_EAX_imm32(VCpu*);

void _TEST_RM8_reg8(VCpu*);
void _TEST_RM16_reg16(VCpu*);
void _TEST_RM32_reg32(VCpu*);
void _TEST_AL_imm8(VCpu*);
void _TEST_AX_imm16(VCpu*);
void _TEST_EAX_imm32(VCpu*);

void _PUSH_SP_8086_80186(VCpu*);
void _PUSH_AX(VCpu*);
void _PUSH_BX(VCpu*);
void _PUSH_CX(VCpu*);
void _PUSH_DX(VCpu*);
void _PUSH_BP(VCpu*);
void _PUSH_SP(VCpu*);
void _PUSH_SI(VCpu*);
void _PUSH_DI(VCpu*);
void _POP_AX(VCpu*);
void _POP_BX(VCpu*);
void _POP_CX(VCpu*);
void _POP_DX(VCpu*);
void _POP_BP(VCpu*);
void _POP_SP(VCpu*);
void _POP_SI(VCpu*);
void _POP_DI(VCpu*);
void _PUSH_CS(VCpu*);
void _PUSH_DS(VCpu*);
void _PUSH_ES(VCpu*);
void _PUSH_SS(VCpu*);
void _PUSHF(VCpu*);

void _POP_DS(VCpu*);
void _POP_ES(VCpu*);
void _POP_SS(VCpu*);
void _POPF(VCpu*);

void _LAHF(VCpu*);
void _SAHF(VCpu*);

void _OUT_imm8_AL(VCpu*);
void _OUT_imm8_AX(VCpu*);
void _OUT_imm8_EAX(VCpu*);
void _OUT_DX_AL(VCpu*);
void _OUT_DX_AX(VCpu*);
void _OUT_DX_EAX(VCpu*);
void _OUTSB(VCpu*);
void _OUTSW(VCpu*);
void _OUTSD(VCpu*);

void _IN_AL_imm8(VCpu*);
void _IN_AX_imm8(VCpu*);
void _IN_EAX_imm8(VCpu*);
void _IN_AL_DX(VCpu*);
void _IN_AX_DX(VCpu*);
void _IN_EAX_DX(VCpu*);

void _ADD_RM8_reg8(VCpu*);
void _ADD_RM16_reg16(VCpu*);
void _ADD_reg8_RM8(VCpu*);
void _ADD_reg16_RM16(VCpu*);
void _ADD_AL_imm8(VCpu*);
void _ADD_AX_imm16(VCpu*);
void _ADD_EAX_imm32(VCpu*);
void _ADD_RM8_imm8(VCpu*);
void _ADD_RM16_imm16(VCpu*);
void _ADD_RM16_imm8(VCpu*);

void _SUB_RM8_reg8(VCpu*);
void _SUB_RM16_reg16(VCpu*);
void _SUB_reg8_RM8(VCpu*);
void _SUB_reg16_RM16(VCpu*);
void _SUB_AL_imm8(VCpu*);
void _SUB_AX_imm16(VCpu*);
void _SUB_EAX_imm32(VCpu*);
void _SUB_RM8_imm8(VCpu*);
void _SUB_RM16_imm16(VCpu*);
void _SUB_RM16_imm8(VCpu*);

void _ADC_RM8_reg8(VCpu*);
void _ADC_RM16_reg16(VCpu*);
void _ADC_reg8_RM8(VCpu*);
void _ADC_reg16_RM16(VCpu*);
void _ADC_AL_imm8(VCpu*);
void _ADC_AX_imm16(VCpu*);
void _ADC_EAX_imm32(VCpu*);
void _ADC_RM8_imm8(VCpu*);
void _ADC_RM16_imm16(VCpu*);
void _ADC_RM16_imm8(VCpu*);

void _SBB_RM8_reg8(VCpu*);
void _SBB_RM16_reg16(VCpu*);
void _SBB_RM32_reg32(VCpu*);
void _SBB_reg8_RM8(VCpu*);
void _SBB_reg16_RM16(VCpu*);
void _SBB_AL_imm8(VCpu*);
void _SBB_AX_imm16(VCpu*);
void _SBB_EAX_imm32(VCpu*);
void _SBB_RM8_imm8(VCpu*);
void _SBB_RM16_imm16(VCpu*);
void _SBB_RM16_imm8(VCpu*);

void _CMP_RM8_reg8(VCpu*);
void _CMP_RM16_reg16(VCpu*);
void _CMP_RM32_reg32(VCpu*);
void _CMP_reg8_RM8(VCpu*);
void _CMP_reg16_RM16(VCpu*);
void _CMP_reg32_RM32(VCpu*);
void _CMP_AL_imm8(VCpu*);
void _CMP_AX_imm16(VCpu*);
void _CMP_EAX_imm32(VCpu*);
void _CMP_RM8_imm8(VCpu*);
void _CMP_RM16_imm16(VCpu*);
void _CMP_RM16_imm8(VCpu*);

void _MUL_RM8(VCpu*);
void _MUL_RM16(VCpu*);
void _MUL_RM32(VCpu*);
void _DIV_RM8(VCpu*);
void _DIV_RM16(VCpu*);
void _DIV_RM32(VCpu*);
void _IMUL_RM8(VCpu*);
void _IMUL_RM16(VCpu*);
void _IMUL_RM32(VCpu*);
void _IDIV_RM8(VCpu*);
void _IDIV_RM16(VCpu*);
void _IDIV_RM32(VCpu*);

void _TEST_RM8_imm8(VCpu*);
void _TEST_RM16_imm16(VCpu*);
void _NOT_RM8(VCpu*);
void _NOT_RM16(VCpu*);
void _NEG_RM8(VCpu*);
void _NEG_RM16(VCpu*);

void _INC_RM16(VCpu*);
void _INC_reg16(VCpu*);
void _INC_reg32(VCpu*);
void _DEC_RM16(VCpu*);
void _DEC_reg16(VCpu*);
void _DEC_reg32(VCpu*);

void _CALL_RM16(VCpu*);
void _CALL_FAR_mem16(VCpu*);
void _CALL_imm16_imm16(VCpu*);
void _CALL_imm16_imm32(VCpu*);

void _JMP_RM16(VCpu*);
void _JMP_FAR_mem16(VCpu*);

void _PUSH_RM16(VCpu*);
void _POP_RM16(VCpu*);
void _POP_RM32(VCpu*);

void _wrap_0x80(VCpu*);
void _wrap_0x81_16(VCpu*);
void _wrap_0x81_32(VCpu*);
void _wrap_0x83_16(VCpu*);
void _wrap_0x83_32(VCpu*);
void _wrap_0x8F_16(VCpu*);
void _wrap_0x8F_32(VCpu*);
void _wrap_0xC0(VCpu*);
void _wrap_0xC1_16(VCpu*);
void _wrap_0xC1_32(VCpu*);
void _wrap_0xD0(VCpu*);
void _wrap_0xD1_16(VCpu*);
void _wrap_0xD1_32(VCpu*);
void _wrap_0xD2(VCpu*);
void _wrap_0xD3_16(VCpu*);
void _wrap_0xD3_32(VCpu*);
void _wrap_0xF6(VCpu*);
void _wrap_0xF7_16(VCpu*);
void _wrap_0xF7_32(VCpu*);
void _wrap_0xFE(VCpu*);
void _wrap_0xFF_16(VCpu*);
void _wrap_0xFF_32(VCpu*);

// 80186+ INSTRUCTIONS

void _wrap_0x0F(VCpu*);

void _BOUND(VCpu*);
void _ENTER(VCpu*);
void _LEAVE(VCpu*);

void _PUSHA(VCpu*);
void _POPA(VCpu*);
void _PUSH_imm8(VCpu*);
void _PUSH_imm16(VCpu*);

void _IMUL_reg16_RM16_imm8(VCpu*);

// 80386+ INSTRUCTIONS

void _LMSW(VCpu*);
void _SMSW(VCpu*);

void _SGDT(VCpu*);
void _LGDT(VCpu*);
void _SIDT(VCpu*);
void _LIDT(VCpu*);

void _PUSHAD(VCpu*);
void _POPAD(VCpu*);
void _PUSHFD(VCpu*);
void _POPFD(VCpu*);
void _PUSH_imm32(VCpu*);

void _PUSH_EAX(VCpu*);
void _PUSH_EBX(VCpu*);
void _PUSH_ECX(VCpu*);
void _PUSH_EDX(VCpu*);
void _PUSH_EBP(VCpu*);
void _PUSH_ESP(VCpu*);
void _PUSH_ESI(VCpu*);
void _PUSH_EDI(VCpu*);

void _POP_EAX(VCpu*);
void _POP_EBX(VCpu*);
void _POP_ECX(VCpu*);
void _POP_EDX(VCpu*);
void _POP_EBP(VCpu*);
void _POP_ESP(VCpu*);
void _POP_ESI(VCpu*);
void _POP_EDI(VCpu*);

void _TEST_RM32_imm32(VCpu*);
void _XOR_RM32_reg32(VCpu*);
void _ADD_RM32_reg32(VCpu*);
void _ADC_RM32_reg32(VCpu*);
void _SUB_RM32_reg32(VCpu*);

void _MOVZX_reg16_RM8(VCpu*);
void _MOVZX_reg32_RM8(VCpu*);
void _MOVZX_reg32_RM16(VCpu*);

void _MOVSD(VCpu*);

void _LFS_reg16_mem16(VCpu*);
void _LFS_reg32_mem32(VCpu*);
void _LGS_reg16_mem16(VCpu*);
void _LGS_reg32_mem32(VCpu*);

void _PUSH_FS(VCpu*);
void _PUSH_GS(VCpu*);
void _POP_FS(VCpu*);
void _POP_GS(VCpu*);

void _FS(VCpu*);
void _GS(VCpu*);

void _STOSD(VCpu*);

void _MOV_RM32_reg32(VCpu*);
void _MOV_reg32_RM32(VCpu*);
void _MOV_moff32_EAX(VCpu*);
void _MOV_EAX_imm32(VCpu*);
void _MOV_EBX_imm32(VCpu*);
void _MOV_ECX_imm32(VCpu*);
void _MOV_EDX_imm32(VCpu*);
void _MOV_EBP_imm32(VCpu*);
void _MOV_ESP_imm32(VCpu*);
void _MOV_ESI_imm32(VCpu*);
void _MOV_EDI_imm32(VCpu*);

void _MOV_seg_RM32(VCpu*);

void _JMP_imm16_imm32(VCpu*);

void _ADD_RM32_imm32(VCpu*);
void _OR_RM32_imm32(VCpu*);
void _ADC_RM32_imm32(VCpu*);
void _SBB_RM32_imm32(VCpu*);
void _AND_RM32_imm32(VCpu*);
void _SUB_RM32_imm32(VCpu*);
void _XOR_RM32_imm32(VCpu*);
void _CMP_RM32_imm32(VCpu*);

void _ADD_RM32_imm8(VCpu*);
void _OR_RM32_imm8(VCpu*);
void _ADC_RM32_imm8(VCpu*);
void _SBB_RM32_imm8(VCpu*);
void _AND_RM32_imm8(VCpu*);
void _SUB_RM32_imm8(VCpu*);
void _XOR_RM32_imm8(VCpu*);
void _CMP_RM32_imm8(VCpu*);

void _ADD_reg32_RM32(VCpu*);
void _OR_reg32_RM32(VCpu*);
void _ADC_reg32_RM32(VCpu*);
void _SBB_reg32_RM32(VCpu*);
void _AND_reg32_RM32(VCpu*);
void _SUB_reg32_RM32(VCpu*);
void _XOR_reg32_RM32(VCpu*);
void _CMP_reg32_RM32(VCpu*);

void _ADD_RM32_reg32(VCpu*);
void _OR_RM32_reg32(VCpu*);
void _ADC_RM32_reg32(VCpu*);
void _SBB_RM32_reg32(VCpu*);
void _AND_RM32_reg32(VCpu*);
void _SUB_RM32_reg32(VCpu*);
void _XOR_RM32_reg32(VCpu*);
void _CMP_RM32_reg32(VCpu*);

// INLINE IMPLEMENTATIONS

#include "vga_memory.h"

BYTE VCpu::readUnmappedMemory8(DWORD address) const
{
    return this->memory[address];
}

WORD VCpu::readUnmappedMemory16(DWORD address) const
{
    return vomit_read16FromPointer(reinterpret_cast<WORD*>(this->memory + address));
}

void VCpu::writeUnmappedMemory8(DWORD address, BYTE value)
{
#ifdef VOMIT_DETECT_UNINITIALIZED_ACCESS
    m_dirtMap[address] = true;
#endif
    this->memory[address] = value;
}

void VCpu::writeUnmappedMemory16(DWORD address, WORD value)
{
#ifdef VOMIT_DETECT_UNINITIALIZED_ACCESS
    m_dirtMap[address] = true;
    m_dirtMap[address + 1] = true;
#endif
    vomit_write16ToPointer(reinterpret_cast<WORD*>(this->memory + address), value);
}

BYTE VCpu::readMemory8(DWORD address) const
{
    if (IS_VGA_MEMORY(address))
        return this->vgaMemory->read8(address);
#ifdef VOMIT_DETECT_UNINITIALIZED_ACCESS
    if (!m_dirtMap[address])
        vlog(VM_MEMORYMSG, "%04X:%04X: Uninitialized read from %08X", getBaseCS(), getBaseIP(), address);
#endif
    return this->memory[address];
}

BYTE VCpu::readMemory8(WORD segment, WORD offset) const
{
    return readMemory8(vomit_toFlatAddress(segment, offset));
}

WORD VCpu::readMemory16(DWORD address) const
{
    if (IS_VGA_MEMORY(address))
        return this->vgaMemory->read16(address);
#ifdef VOMIT_DETECT_UNINITIALIZED_ACCESS
    if (!m_dirtMap[address] || !m_dirtMap[address + 1])
        vlog(VM_MEMORYMSG, "%04X:%04X: Uninitialized read from %08X", getBaseCS(), getBaseIP(), address);
#endif
    return vomit_read16FromPointer(reinterpret_cast<WORD*>(this->memory + address));
}

WORD VCpu::readMemory16(WORD segment, WORD offset) const
{
    return readMemory16(vomit_toFlatAddress(segment, offset));
}

DWORD VCpu::readMemory32(WORD segment, WORD offset) const
{
    return readMemory32(vomit_toFlatAddress(segment, offset));
}

void VCpu::writeMemory8(DWORD address, BYTE value)
{
    if (IS_VGA_MEMORY(address)) {
        this->vgaMemory->write8(address, value);
        return;
    }

#ifdef VOMIT_DETECT_UNINITIALIZED_ACCESS
    m_dirtMap[address] = true;
#endif

    this->memory[address] = value;
}

void VCpu::writeMemory8(WORD segment, WORD offset, BYTE value)
{
    writeMemory8(vomit_toFlatAddress(segment, offset), value);
}

void VCpu::writeMemory16(DWORD address, WORD value)
{
    if (IS_VGA_MEMORY(address)) {
        this->vgaMemory->write16(address, value);
        return;
    }

#ifdef VOMIT_DETECT_UNINITIALIZED_ACCESS
    m_dirtMap[address] = true;
    m_dirtMap[address + 1] = true;
#endif

    WORD* ptr = reinterpret_cast<WORD*>(this->memory + address);
    vomit_write16ToPointer(ptr, value);
}

void VCpu::writeMemory16(WORD segment, WORD offset, WORD value)
{
    writeMemory16(FLAT(segment, offset), value);
}

void VCpu::writeMemory32(WORD segment, WORD offset, DWORD value)
{
    writeMemory32(FLAT(segment, offset), value);
}

BYTE* VCpu::codeMemory() const
{
    return m_codeMemory;
}

BYTE* VCpu::memoryPointer(WORD segment, WORD offset) const
{
    return this->memory + vomit_toFlatAddress(segment, offset);
}

#ifndef VOMIT_PREFETCH_QUEUE
WORD VCpu::fetchOpcodeWord()
{
    WORD w = *reinterpret_cast<WORD*>(&m_codeMemory[getIP()]);
    this->IP += 2;
#ifdef VOMIT_BIG_ENDIAN
    return V_BYTESWAP(w);
#else
    return w;
#endif
}
#endif

DWORD VCpu::fetchOpcodeDWord()
{
    DWORD d = *reinterpret_cast<DWORD*>(&m_codeMemory[getIP()]);
    this->IP += 4;
#ifdef VOMIT_BIG_ENDIAN
#error IMPLEMENT ME
#else
    return d;
#endif
}

bool VCpu::tick()
{
    if (--m_pitCountdown == 0) {
        m_pitCountdown = m_instructionsPerTick;
        return true;
    }
    return false;
}

#include "debug.h"

bool VCpu::evaluate(BYTE conditionCode) const
{
    VM_ASSERT(conditionCode <= 0xF);

    switch (conditionCode) {
    case  0: return this->OF;                            // O
    case  1: return !this->OF;                           // NO
    case  2: return this->CF;                            // B, C, NAE
    case  3: return !this->CF;                           // NB, NC, AE
    case  4: return this->ZF;                            // E, Z
    case  5: return !this->ZF;                           // NE, NZ
    case  6: return (this->CF | this->ZF);               // BE, NA
    case  7: return !(this->CF | this->ZF);              // NBE, A
    case  8: return this->SF;                            // S
    case  9: return !this->SF;                           // NS
    case 10: return this->PF;                            // P, PE
    case 11: return !this->PF;                           // NP, PO
    case 12: return this->SF ^ this->OF;                 // L, NGE
    case 13: return !(this->SF ^ this->OF);              // NL, GE
    case 14: return (this->SF ^ this->OF) | this->ZF;    // LE, NG
    case 15: return !((this->SF ^ this->OF) | this->ZF); // NLE, G
    }
    return 0;
}

#endif
