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
    if (insn.register_index() >= 6) {
        throw InvalidOpcode("MOV_RM16_seg with invalid segment register index");
    }
    insn.modrm().write_special(insn.segreg(), o32());
}

void CPU::_MOV_seg_RM16(Instruction& insn)
{
    if (insn.segment_register_index() == SegmentRegisterIndex::CS)
        throw InvalidOpcode("MOV CS");
    write_segment_register(insn.segment_register_index(), insn.modrm().read16());
    if (insn.segment_register_index() == SegmentRegisterIndex::SS) {
        make_next_instruction_uninterruptible();
    }
}

void CPU::_MOV_seg_RM32(Instruction& insn)
{
    if (insn.segment_register_index() == SegmentRegisterIndex::CS)
        throw InvalidOpcode("MOV CS");
    write_segment_register(insn.segment_register_index(), insn.modrm().read32());
    if (insn.segment_register_index() == SegmentRegisterIndex::SS) {
        make_next_instruction_uninterruptible();
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
    int crIndex = insn.register_index();

    if (!isValidControlRegisterIndex(crIndex))
        throw InvalidOpcode("MOV_reg32_CR with invalid control register");

    if (get_vm()) {
        throw GeneralProtectionFault(0, "MOV reg32, CRx with VM=1");
    }

    if (get_pe()) {
        // FIXME: Other GP(0) conditions:
        // If an attempt is made to write invalid bit combinations in CR0
        // (such as setting the PG flag to 1 when the PE flag is set to 0, or
        // setting the CD flag to 0 when the NW flag is set to 1).
        // If an attempt is made to write a 1 to any reserved bit in CR4.
        // If an attempt is made to write 1 to CR4.PCIDE.
        // If any of the reserved bits are set in the page-directory pointers
        // table (PDPT) and the loading of a control register causes the
        // PDPT to be loaded into the processor.
        if (get_cpl() != 0) {
            throw GeneralProtectionFault(0, QString("MOV reg32, CRx with CPL!=0(%1)").arg(get_cpl()));
        }
    } else {
        // FIXME: GP(0) conditions:
        // If an attempt is made to write a 1 to any reserved bit in CR4.
        // If an attempt is made to write 1 to CR4.PCIDE.
        // If an attempt is made to write invalid bit combinations in CR0
        // (such as setting the PG flag to 1 when the PE flag is set to 0).
    }

    write_register<u32>(insn.rm() & 7, get_control_register(crIndex));
}

void CPU::_MOV_CR_reg32(Instruction& insn)
{
    int crIndex = insn.register_index();

    if (!isValidControlRegisterIndex(crIndex))
        throw InvalidOpcode("MOV_CR_reg32 with invalid control register");

    if (get_vm()) {
        throw GeneralProtectionFault(0, "MOV CRx, reg32 with VM=1");
    }

    if (get_pe()) {
        // FIXME: Other GP(0) conditions:
        // If an attempt is made to write invalid bit combinations in CR0
        // (such as setting the PG flag to 1 when the PE flag is set to 0, or
        // setting the CD flag to 0 when the NW flag is set to 1).
        // If an attempt is made to write a 1 to any reserved bit in CR4.
        // If an attempt is made to write 1 to CR4.PCIDE.
        // If any of the reserved bits are set in the page-directory pointers
        // table (PDPT) and the loading of a control register causes the
        // PDPT to be loaded into the processor.
        if (get_cpl() != 0) {
            throw GeneralProtectionFault(0, QString("MOV CRx, reg32 with CPL!=0(%1)").arg(get_cpl()));
        }
    } else {
        // FIXME: GP(0) conditions:
        // If an attempt is made to write a 1 to any reserved bit in CR4.
        // If an attempt is made to write 1 to CR4.PCIDE.
        // If an attempt is made to write invalid bit combinations in CR0
        // (such as setting the PG flag to 1 when the PE flag is set to 0).
    }

    auto value = read_register<u32>(static_cast<CPU::RegisterIndex32>(insn.rm() & 7));

    if (crIndex == 4) {
        vlog(LogCPU, "CR4 written (%08x) but not supported!", value);
    }
    set_control_register(crIndex, value);

    if (crIndex == 0 || crIndex == 3)
        update_code_segment_cache();

#ifdef VERBOSE_DEBUG
    vlog(LogCPU, "MOV CR%u <- %08X", crIndex, getControlRegister(crIndex));
#endif
}

void CPU::_MOV_reg32_DR(Instruction& insn)
{
    int drIndex = insn.register_index();
    auto register_index = static_cast<CPU::RegisterIndex32>(insn.rm() & 7);

    if (get_vm()) {
        throw GeneralProtectionFault(0, "MOV reg32, DRx with VM=1");
    }

    if (get_pe()) {
        if (get_cpl() != 0) {
            throw GeneralProtectionFault(0, QString("MOV reg32, DRx with CPL!=0(%1)").arg(get_cpl()));
        }
    }

    write_register(register_index, get_debug_register(drIndex));
    vlog(LogCPU, "MOV %s <- DR%u (%08X)", register_name(register_index), drIndex, get_debug_register(drIndex));
}

void CPU::_MOV_DR_reg32(Instruction& insn)
{
    int drIndex = insn.register_index();
    auto register_index = static_cast<CPU::RegisterIndex32>(insn.rm() & 7);

    if (get_vm()) {
        throw GeneralProtectionFault(0, "MOV DRx, reg32 with VM=1");
    }

    if (get_pe()) {
        if (get_cpl() != 0) {
            throw GeneralProtectionFault(0, QString("MOV DRx, reg32 with CPL!=0(%1)").arg(get_cpl()));
        }
    }

    set_debug_register(drIndex, read_register<u32>(register_index));
    //vlog(LogCPU, "MOV DR%u <- %08X", drIndex, getDebugRegister(drIndex));
}

void CPU::_MOV_reg8_imm8(Instruction& insn)
{
    write_register<u8>(insn.register_index(), insn.imm8());
}

void CPU::_MOV_reg16_imm16(Instruction& insn)
{
    write_register<u16>(insn.register_index(), insn.imm16());
}

void CPU::_MOV_reg32_imm32(Instruction& insn)
{
    write_register<u32>(insn.register_index(), insn.imm32());
}

template<typename T>
void CPU::doMOV_Areg_moff(Instruction& insn)
{
    write_register<T>(RegisterAL, read_memory<T>(current_segment(), insn.imm_address()));
}

void CPU::_MOV_AL_moff8(Instruction& insn)
{
    doMOV_Areg_moff<u8>(insn);
}

void CPU::_MOV_AX_moff16(Instruction& insn)
{
    doMOV_Areg_moff<u16>(insn);
}

void CPU::_MOV_EAX_moff32(Instruction& insn)
{
    doMOV_Areg_moff<u32>(insn);
}

template<typename T>
void CPU::doMOV_moff_Areg(Instruction& insn)
{
    write_memory<T>(current_segment(), insn.imm_address(), read_register<T>(RegisterAL));
}

void CPU::_MOV_moff8_AL(Instruction& insn)
{
    doMOV_moff_Areg<u8>(insn);
}

void CPU::_MOV_moff16_AX(Instruction& insn)
{
    doMOV_moff_Areg<u16>(insn);
}

void CPU::_MOV_moff32_EAX(Instruction& insn)
{
    doMOV_moff_Areg<u32>(insn);
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
    insn.reg16() = signExtendedTo<u16>(insn.modrm().read8());
}

void CPU::_MOVSX_reg32_RM8(Instruction& insn)
{
    insn.reg32() = signExtendedTo<u32>(insn.modrm().read8());
}

void CPU::_MOVSX_reg32_RM16(Instruction& insn)
{
    insn.reg32() = signExtendedTo<u32>(insn.modrm().read16());
}

void CPU::_CMPXCHG_RM32_reg32(Instruction& insn)
{
    auto current = insn.modrm().read32();
    if (current == get_eax()) {
        set_zf(1);
        insn.modrm().write32(insn.reg32());
    } else {
        set_zf(0);
        set_eax(current);
    }
}

void CPU::_CMPXCHG_RM16_reg16(Instruction& insn)
{
    auto current = insn.modrm().read16();
    if (current == get_ax()) {
        set_zf(1);
        insn.modrm().write16(insn.reg16());
    } else {
        set_zf(0);
        set_ax(current);
    }
}
