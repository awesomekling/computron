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
#include "iodevice.h"
#include "machine.h"

void CPU::_OUT_imm8_AL(Instruction& insn)
{
    out8(insn.imm8(), getAL());
}

void CPU::_OUT_imm8_AX(Instruction& insn)
{
    out16(insn.imm8(), getAX());
}

void CPU::_OUT_imm8_EAX(Instruction& insn)
{
    out32(insn.imm8(), getEAX());
}

void CPU::_OUT_DX_AL(Instruction&)
{
    out8(getDX(), getAL());
}

void CPU::_OUT_DX_AX(Instruction&)
{
    out16(getDX(), getAX());
}

void CPU::_OUT_DX_EAX(Instruction&)
{
    out32(getDX(), getEAX());
}

void CPU::_IN_AL_imm8(Instruction& insn)
{
    setAL(in8(insn.imm8()));
}

void CPU::_IN_AX_imm8(Instruction& insn)
{
    setAX(in16(insn.imm8()));
}

void CPU::_IN_EAX_imm8(Instruction& insn)
{
    setEAX(in32(insn.imm8()));
}

void CPU::_IN_AL_DX(Instruction&)
{
    setAL(in8(getDX()));
}

void CPU::_IN_AX_DX(Instruction&)
{
    setAX(in16(getDX()));
}

void CPU::_IN_EAX_DX(Instruction&)
{
    setEAX(in32(getDX()));
}

template<typename T>
void CPU::validateIOAccess(u16 port)
{
    if (!getPE())
        return;
    if (!getVM() && !(getCPL() > getIOPL()))
        return;
    auto tss = currentTSS();
    if (!tss.is32Bit()) {
        vlog(LogCPU, "validateIOAccess for 16-bit TSS, what do?");
        ASSERT_NOT_REACHED();
    }

    if (TR.limit < 103)
        throw GeneralProtectionFault(0, "TSS too small, I/O map missing");

    u16 iomapBase = tss.getIOMapBase();
    u16 highPort = port + sizeof(T) - 1;

    if (TR.limit < (iomapBase + highPort / 8))
        throw GeneralProtectionFault(0, "TSS I/O map too small");

    u16 mask = (1 << (sizeof(T) - 1)) << (port & 7);
    LinearAddress address = TR.base.offset(iomapBase + (port / 8));
    u16 perm = mask & 0xff00 ? readMemory16(address) : readMemory8(address);
    if (perm & mask)
        throw GeneralProtectionFault(0, "I/O map disallowed access");
}

// Important note from IA32 manual, regarding string I/O instructions:
// "These instructions may read from the I/O port without writing to the memory location if an exception or VM exit
// occurs due to the write (e.g. #PF). If this would be problematic, for example because the I/O port read has side-
// effects, software should ensure the write to the memory location does not cause an exception or VM exit."

template<typename T>
void CPU::out(u16 port, T data)
{
    validateIOAccess<T>(port);

    if (options.iopeek) {
        if (port != 0x00E6 && port != 0x0020 && port != 0x3D4 && port != 0x03d5 && port != 0xe2 && port != 0xe0 && port != 0x92) {
            vlog(LogIO, "CPU::out<%zu>: %x --> %03x", sizeof(T) * 8, data, port);
        }
    }

    if (auto* device = machine().outputDeviceForPort(port)) {
        device->out<T>(port, data);
        return;
    }

    if (!IODevice::shouldIgnorePort(port))
        vlog(LogAlert, "Unhandled I/O write to port %03x, data %x", port, data);
}

template<typename T>
T CPU::in(u16 port)
{
    validateIOAccess<T>(port);

    T data;
    if (auto* device = machine().inputDeviceForPort(port)) {
        data = device->in<T>(port);
    } else {
        if (!IODevice::shouldIgnorePort(port))
            vlog(LogAlert, "Unhandled I/O read from port %03x", port);
        data = IODevice::JunkValue;
    }

    if (options.iopeek) {
        if (port != 0xe6 && port != 0x20 && port != 0x3d4 && port != 0x03d5 && port != 0x3da && port != 0x92) {
            vlog(LogIO, "CPU::in<%zu>: %03x = %x", sizeof(T) * 8, port, data);
        }
    }
    return data;
}

void CPU::out8(u16 port, u8 data)
{
    out<u8>(port, data);
}

void CPU::out16(u16 port, u16 data)
{
    out<u16>(port, data);
}

void CPU::out32(u16 port, u32 data)
{
    out<u32>(port, data);
}

u8 CPU::in8(u16 port)
{
    return in<u8>(port);
}

u16 CPU::in16(u16 port)
{
    return in<u16>(port);
}

u32 CPU::in32(u16 port)
{
    return in<u32>(port);
}

template u8 CPU::in<u8>(u16 port);
template u16 CPU::in<u16>(u16 port);
template u32 CPU::in<u32>(u16 port);
template void CPU::out<u8>(u16 port, u8);
template void CPU::out<u16>(u16 port, u16);
template void CPU::out<u32>(u16 port, u32);
