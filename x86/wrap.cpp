/*
 * Copyright (C) 2003-2018 Andreas Kling <awesomekling@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY ANDREAS KLING ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ANDREAS KLING OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "vcpu.h"
#include "debug.h"
#include "debugger.h"

template<typename T>
T VCpu::doRol(T data, int steps)
{
    T result = data;
    steps &= 0x1F;
    if (!steps)
        return data;

    setCF(data >> (BitSizeOfType<T>::bits - steps) & 1);
    if ((steps &= BitSizeOfType<T>::bits - 1))
        result = (data << steps) | (data >> (BitSizeOfType<T>::bits - steps));
    if (steps == 1)
        setOF(((data >> (BitSizeOfType<T>::bits - 1)) & 1) ^ getCF());

    return result;
}

template<typename T>
T VCpu::doRor(T data, int steps)
{
    T result = data;
    steps &= 0x1F;
    if (!steps)
        return data;

    setCF((result >> (steps - 1)) & 1);
    if ((steps &= BitSizeOfType<T>::bits - 1))
        result = (data >> steps) | (data << (BitSizeOfType<T>::bits - steps));
    if (steps == 1)
        setOF((result >> (BitSizeOfType<T>::bits - 1)) ^ ((result >> (BitSizeOfType<T>::bits - 2) & 1)));

    return result;
}

template<typename T>
T VCpu::rightShift(T data, int steps)
{
    T result = data;
    steps &= 0x1F;
    if (!steps)
        return data;

    if (steps <= BitSizeOfType<T>::bits) {
        setCF((result >> (steps - 1)) & 1);
        if (steps == 1)
            setOF((data >> (BitSizeOfType<T>::bits - 1)) & 1);
    }
    result >>= steps;

    updateFlags(result, BitSizeOfType<T>::bits);
    return result;
}

template<typename T>
T VCpu::leftShift(T data, int steps)
{
    T result = data;
    steps &= 0x1F;
    if (!steps)
        return data;

    if (steps <= BitSizeOfType<T>::bits) {
        setCF(result >> (BitSizeOfType<T>::bits - steps) & 1);
        if (steps == 1)
            setOF((data >> (BitSizeOfType<T>::bits - 1)) ^ getCF());
    }
    result <<= steps;

    updateFlags(result, BitSizeOfType<T>::bits);
    return result;
}

void VCpu::_wrap_0xC0(Instruction& insn)
{
    auto& modrm = insn.modrm();
    auto value = modrm.read8();
    BYTE imm = insn.imm8();

    switch (insn.slash()) {
    case 0: modrm.write8(doRol(value, imm)); break;
    case 1: modrm.write8(doRor(value, imm)); break;
    case 2: modrm.write8(cpu_rcl(*this, value, imm, 8)); break;
    case 3: modrm.write8(cpu_rcr(*this, value, imm, 8)); break;
    case 4: modrm.write8(leftShift(value, imm)); break;
    case 5: modrm.write8(rightShift(value, imm)); break;
    case 6:
        vlog(LogAlert, "C0 /6 not wrapped");
        exception(6);
        break;
    case 7: modrm.write8(cpu_sar(*this, value, imm, 8)); break;
    }
}

void VCpu::_wrap_0xC1_16(Instruction& insn)
{
    auto& modrm = insn.modrm();
    auto value = modrm.read16();
    BYTE imm = insn.imm8();

    switch (insn.slash()) {
    case 0: modrm.write16(doRol(value, imm)); break;
    case 1: modrm.write16(doRor(value, imm)); break;
    case 2: modrm.write16(cpu_rcl(*this, value, imm, 16)); break;
    case 3: modrm.write16(cpu_rcr(*this, value, imm, 16)); break;
    case 4: modrm.write16(leftShift(value, imm)); break;
    case 5: modrm.write16(rightShift(value, imm)); break;
    case 6:
        vlog(LogAlert, "[16bit] C1 /6 not wrapped");
        exception(6);
        break;
    case 7: modrm.write16(cpu_sar(*this, value, imm, 16)); break;
    }
}

void VCpu::_wrap_0xC1_32(Instruction& insn)
{
    auto& modrm = insn.modrm();
    auto value = modrm.read32();
    BYTE imm = insn.imm8();

    switch (insn.slash()) {
    case 0: modrm.write32(doRol(value, imm)); break;
    case 1: modrm.write32(doRor(value, imm)); break;
    case 2: modrm.write32(cpu_rcl(*this, value, imm, 32)); break;
    case 3: modrm.write32(cpu_rcr(*this, value, imm, 32)); break;
    case 4: modrm.write32(leftShift(value, imm)); break;
    case 5: modrm.write32(rightShift(value, imm)); break;
    case 6:
        vlog(LogAlert, "[32bit] C1 /6 not wrapped");
        exception(6);
        break;
    case 7: modrm.write32(cpu_sar(*this, value, imm, 32)); break;
    }
}

void VCpu::_wrap_0xD0(Instruction& insn)
{
    auto& modrm = insn.modrm();
    auto value = modrm.read8();

    switch (insn.slash()) {
    case 0: modrm.write8(doRol(value, 1)); break;
    case 1: modrm.write8(doRor(value, 1)); break;
    case 2: modrm.write8(cpu_rcl(*this, value, 1, 8 )); break;
    case 3: modrm.write8(cpu_rcr(*this, value, 1, 8 )); break;
    case 4: modrm.write8(leftShift(value, 1)); break;
    case 5: modrm.write8(rightShift(value, 1)); break;
    case 6:
        vlog(LogAlert, "D0 /6 not wrapped");
        exception(6);
        break;
    case 7: modrm.write8(cpu_sar(*this, value, 1, 8 )); break;
    }
}

void VCpu::_wrap_0xD1_16(Instruction& insn)
{
    auto& modrm = insn.modrm();
    auto value = modrm.read16();

    switch (insn.slash()) {
    case 0: modrm.write16(doRol(value, 1)); break;
    case 1: modrm.write16(doRor(value, 1)); break;
    case 2: modrm.write16(cpu_rcl(*this, value, 1, 16)); break;
    case 3: modrm.write16(cpu_rcr(*this, value, 1, 16)); break;
    case 4: modrm.write16(leftShift(value, 1)); break;
    case 5: modrm.write16(rightShift(value, 1)); break;
    case 6:
        vlog(LogAlert, "[16bit] D1 /6 not wrapped");
        exception(6);
        break;
    case 7: modrm.write16(cpu_sar(*this, value, 1, 16)); break;
    }
}

void VCpu::_wrap_0xD1_32(Instruction& insn)
{
    auto& modrm = insn.modrm();
    auto value = modrm.read32();

    switch (insn.slash()) {
    case 0: modrm.write32(doRol(value, 1)); break;
    case 1: modrm.write32(doRor(value, 1)); break;
    case 2: modrm.write32(cpu_rcl(*this, value, 1, 32)); break;
    case 3: modrm.write32(cpu_rcr(*this, value, 1, 32)); break;
    case 4: modrm.write32(leftShift(value, 1)); break;
    case 5: modrm.write32(rightShift(value, 1)); break;
    case 6:
        vlog(LogAlert, "[32bit] D1 /6 not wrapped");
        exception(6);
        break;
    case 7: modrm.write32(cpu_sar(*this, value, 1, 32)); break;
    }
}

void VCpu::_wrap_0xD2(Instruction& insn)
{
    auto& modrm = insn.modrm();
    auto value = modrm.read8();

    switch (insn.slash()) {
    case 0: modrm.write8(doRol(value, regs.B.CL)); break;
    case 1: modrm.write8(doRor(value, regs.B.CL)); break;
    case 2: modrm.write8(cpu_rcl(*this, value, regs.B.CL, 8 )); break;
    case 3: modrm.write8(cpu_rcr(*this, value, regs.B.CL, 8 )); break;
    case 4: modrm.write8(leftShift(value, regs.B.CL)); break;
    case 5: modrm.write8(rightShift(value, regs.B.CL)); break;
    case 6:
        vlog(LogAlert, "D2 /6 not wrapped");
        exception(6);
        break;
    case 7: modrm.write8(cpu_sar(*this, value, regs.B.CL, 8 )); break;
    }
}

void VCpu::_wrap_0xD3_16(Instruction& insn)
{
    auto& modrm = insn.modrm();
    auto value = modrm.read16();

    switch (insn.slash()) {
    case 0: modrm.write16(doRol(value, regs.B.CL)); break;
    case 1: modrm.write16(doRor(value, regs.B.CL)); break;
    case 2: modrm.write16(cpu_rcl(*this, value, regs.B.CL, 16)); break;
    case 3: modrm.write16(cpu_rcr(*this, value, regs.B.CL, 16)); break;
    case 4: modrm.write16(leftShift(value, regs.B.CL)); break;
    case 5: modrm.write16(rightShift(value, regs.B.CL)); break;
    case 6:
        vlog(LogAlert, "[16bit] D3 /6 not wrapped");
        exception(6);
        break;
    case 7: modrm.write16(cpu_sar(*this, value, regs.B.CL, 16)); break;
    }
}

void VCpu::_wrap_0xD3_32(Instruction& insn)
{
    auto& modrm = insn.modrm();
    auto value = modrm.read32();

    switch (insn.slash()) {
    case 0: modrm.write32(doRol(value, regs.B.CL)); break;
    case 1: modrm.write32(doRor(value, regs.B.CL)); break;
    case 2: modrm.write32(cpu_rcl(*this, value, regs.B.CL, 32)); break;
    case 3: modrm.write32(cpu_rcr(*this, value, regs.B.CL, 32)); break;
    case 4: modrm.write32(leftShift(value, regs.B.CL)); break;
    case 5: modrm.write32(rightShift(value, regs.B.CL)); break;
    case 6:
        vlog(LogAlert, "[32bit] D3 /6 not wrapped");
        exception(6);
        break;
    case 7: modrm.write32(cpu_sar(*this, value, regs.B.CL, 32)); break;
    }
}
