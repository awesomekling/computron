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
#include "debug.h"

void CPU::_MOV_RM8_imm8(Instruction& insn)
{
    insn.modrm().write8(insn.imm8());
}

void CPU::_MOV_RM16_imm16(Instruction& insn)
{
    insn.modrm().write16(insn.imm16());
}

void CPU::_MOV_RM32_imm32(Instruction& insn)
{
    insn.modrm().write32(insn.imm32());
}

void CPU::_MOV_RM16_seg(Instruction& insn)
{
    if (insn.registerIndex() >= 6) {
        throw InvalidOpcode("MOV_RM16_seg with invalid segment register index");
    }
    insn.modrm().writeSpecial(insn.segreg(), o32());
}

void CPU::_MOV_seg_RM16(Instruction& insn)
{
    if (insn.segmentRegisterIndex() == SegmentRegisterIndex::CS)
        throw InvalidOpcode("MOV CS");
    writeSegmentRegister(insn.segmentRegisterIndex(), insn.modrm().read16());
    if (insn.segmentRegisterIndex() == SegmentRegisterIndex::SS) {
        makeNextInstructionUninterruptible();
    }
}

void CPU::_MOV_seg_RM32(Instruction& insn)
{
    if (insn.segmentRegisterIndex() == SegmentRegisterIndex::CS)
        throw InvalidOpcode("MOV CS");
    writeSegmentRegister(insn.segmentRegisterIndex(), insn.modrm().read32());
    if (insn.segmentRegisterIndex() == SegmentRegisterIndex::SS) {
        makeNextInstructionUninterruptible();
    }
}

void CPU::_MOV_RM8_reg8(Instruction& insn)
{
    insn.modrm().write8(insn.reg8());
}

void CPU::_MOV_reg8_RM8(Instruction& insn)
{
    insn.reg8() = insn.modrm().read8();
}

void CPU::_MOV_RM16_reg16(Instruction& insn)
{
    insn.modrm().write16(insn.reg16());
}

void CPU::_MOV_RM32_reg32(Instruction& insn)
{
    insn.modrm().write32(insn.reg32());
}

void CPU::_MOV_reg16_RM16(Instruction& insn)
{
    insn.reg16() = insn.modrm().read16();
}

void CPU::_MOV_reg32_RM32(Instruction& insn)
{
    insn.reg32() = insn.modrm().read32();
}

static bool isValidControlRegisterIndex(int index)
{
    return index == 0 || index == 2 || index == 3 || index == 4;
}

void CPU::_MOV_reg32_CR(Instruction& insn)
{
    int crIndex = insn.registerIndex();

    if (!isValidControlRegisterIndex(crIndex))
        throw InvalidOpcode("MOV_reg32_CR with invalid control register");

    if (getVM()) {
        throw GeneralProtectionFault(0, "MOV reg32, CRx with VM=1");
    }

    if (getPE()) {
        // FIXME: Other GP(0) conditions:
        // If an attempt is made to write invalid bit combinations in CR0
        // (such as setting the PG flag to 1 when the PE flag is set to 0, or
        // setting the CD flag to 0 when the NW flag is set to 1).
        // If an attempt is made to write a 1 to any reserved bit in CR4.
        // If an attempt is made to write 1 to CR4.PCIDE.
        // If any of the reserved bits are set in the page-directory pointers
        // table (PDPT) and the loading of a control register causes the
        // PDPT to be loaded into the processor.
        if (getCPL() != 0) {
            throw GeneralProtectionFault(0, QString("MOV reg32, CRx with CPL!=0(%1)").arg(getCPL()));
        }
    } else {
        // FIXME: GP(0) conditions:
        // If an attempt is made to write a 1 to any reserved bit in CR4.
        // If an attempt is made to write 1 to CR4.PCIDE.
        // If an attempt is made to write invalid bit combinations in CR0
        // (such as setting the PG flag to 1 when the PE flag is set to 0).
    }

    writeRegister<DWORD>(insn.rm() & 7, getControlRegister(crIndex));
}

void CPU::_MOV_CR_reg32(Instruction& insn)
{
    int crIndex = insn.registerIndex();

    if (!isValidControlRegisterIndex(crIndex))
        throw InvalidOpcode("MOV_CR_reg32 with invalid control register");

    if (getVM()) {
        throw GeneralProtectionFault(0, "MOV CRx, reg32 with VM=1");
    }

    if (getPE()) {
        // FIXME: Other GP(0) conditions:
        // If an attempt is made to write invalid bit combinations in CR0
        // (such as setting the PG flag to 1 when the PE flag is set to 0, or
        // setting the CD flag to 0 when the NW flag is set to 1).
        // If an attempt is made to write a 1 to any reserved bit in CR4.
        // If an attempt is made to write 1 to CR4.PCIDE.
        // If any of the reserved bits are set in the page-directory pointers
        // table (PDPT) and the loading of a control register causes the
        // PDPT to be loaded into the processor.
        if (getCPL() != 0) {
            throw GeneralProtectionFault(0, QString("MOV CRx, reg32 with CPL!=0(%1)").arg(getCPL()));
        }
    } else {
        // FIXME: GP(0) conditions:
        // If an attempt is made to write a 1 to any reserved bit in CR4.
        // If an attempt is made to write 1 to CR4.PCIDE.
        // If an attempt is made to write invalid bit combinations in CR0
        // (such as setting the PG flag to 1 when the PE flag is set to 0).
    }

    if (crIndex == 4) {
        vlog(LogCPU, "cr4 written but not supported!");
        ASSERT_NOT_REACHED();
    }
    setControlRegister(crIndex, readRegister<DWORD>(static_cast<CPU::RegisterIndex32>(insn.rm() & 7)));

    if (crIndex == 0 || crIndex == 3)
        updateCodeSegmentCache();

#ifdef VERBOSE_DEBUG
    vlog(LogCPU, "MOV CR%u <- %08X", crIndex, getControlRegister(crIndex));
#endif
}

void CPU::_MOV_reg32_DR(Instruction& insn)
{
    int drIndex = insn.registerIndex();
    auto registerIndex = static_cast<CPU::RegisterIndex32>(insn.rm() & 7);

    if (getVM()) {
        throw GeneralProtectionFault(0, "MOV reg32, DRx with VM=1");
    }

    if (getPE()) {
        if (getCPL() != 0) {
            throw GeneralProtectionFault(0, QString("MOV reg32, DRx with CPL!=0(%1)").arg(getCPL()));
        }
    }

    writeRegister(registerIndex, getDebugRegister(drIndex));
    vlog(LogCPU, "MOV %s <- DR%u (%08X)", registerName(registerIndex), drIndex, getDebugRegister(drIndex));
}

void CPU::_MOV_DR_reg32(Instruction& insn)
{
    int drIndex = insn.registerIndex();
    auto registerIndex = static_cast<CPU::RegisterIndex32>(insn.rm() & 7);

    if (getVM()) {
        throw GeneralProtectionFault(0, "MOV DRx, reg32 with VM=1");
    }

    if (getPE()) {
        if (getCPL() != 0) {
            throw GeneralProtectionFault(0, QString("MOV DRx, reg32 with CPL!=0(%1)").arg(getCPL()));
        }
    }

    setDebugRegister(drIndex, readRegister<DWORD>(registerIndex));
    //vlog(LogCPU, "MOV DR%u <- %08X", drIndex, getDebugRegister(drIndex));
}

void CPU::_MOV_reg8_imm8(Instruction& insn)
{
    writeRegister<BYTE>(insn.registerIndex(), insn.imm8());
}

void CPU::_MOV_reg16_imm16(Instruction& insn)
{
    writeRegister<WORD>(insn.registerIndex(), insn.imm16());
}

void CPU::_MOV_reg32_imm32(Instruction& insn)
{
    writeRegister<DWORD>(insn.registerIndex(), insn.imm32());
}

template<typename T>
void CPU::doMOV_Areg_moff(Instruction& insn)
{
    writeRegister<T>(RegisterAL, readMemory<T>(currentSegment(), insn.immAddress()));
}

void CPU::_MOV_AL_moff8(Instruction& insn)
{
    doMOV_Areg_moff<BYTE>(insn);
}

void CPU::_MOV_AX_moff16(Instruction& insn)
{
    doMOV_Areg_moff<WORD>(insn);
}

void CPU::_MOV_EAX_moff32(Instruction& insn)
{
    doMOV_Areg_moff<DWORD>(insn);
}

template<typename T>
void CPU::doMOV_moff_Areg(Instruction& insn)
{
    writeMemory<T>(currentSegment(), insn.immAddress(), readRegister<T>(RegisterAL));
}

void CPU::_MOV_moff8_AL(Instruction& insn)
{
    doMOV_moff_Areg<BYTE>(insn);
}

void CPU::_MOV_moff16_AX(Instruction& insn)
{
    doMOV_moff_Areg<WORD>(insn);
}

void CPU::_MOV_moff32_EAX(Instruction& insn)
{
    doMOV_moff_Areg<DWORD>(insn);
}

void CPU::_MOVZX_reg16_RM8(Instruction& insn)
{
    insn.reg16() = insn.modrm().read8();
}

void CPU::_MOVZX_reg32_RM8(Instruction& insn)
{
    insn.reg32() = insn.modrm().read8();
}

void CPU::_MOVZX_reg32_RM16(Instruction& insn)
{
    insn.reg32() = insn.modrm().read16();
}

void CPU::_MOVSX_reg16_RM8(Instruction& insn)
{
    insn.reg16() = signExtendedTo<WORD>(insn.modrm().read8());
}

void CPU::_MOVSX_reg32_RM8(Instruction& insn)
{
    insn.reg32() = signExtendedTo<DWORD>(insn.modrm().read8());
}

void CPU::_MOVSX_reg32_RM16(Instruction& insn)
{
    insn.reg32() = signExtendedTo<DWORD>(insn.modrm().read16());
}

void CPU::_CMPXCHG_RM32_reg32(Instruction& insn)
{
    auto current = insn.modrm().read32();
    if (current == getEAX()) {
        setZF(1);
        insn.modrm().write32(insn.reg32());
    } else {
        setZF(0);
        setEAX(current);
    }
}

void CPU::_CMPXCHG_RM16_reg16(Instruction& insn)
{
    auto current = insn.modrm().read16();
    if (current == getAX()) {
        setZF(1);
        insn.modrm().write16(insn.reg16());
    } else {
        setZF(0);
        setAX(current);
    }
}

