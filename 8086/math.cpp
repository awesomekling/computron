/* 8086/math.c
 * Mathematical instuctions
 *
 */

#include "vomit.h"
#include "templates.h"
#include "debug.h"

WORD cpu_add8(vomit_cpu_t *cpu, BYTE dest, BYTE src)
{
    WORD result = dest + src;
    vomit_cpu_math_flags8(cpu, result, dest, src);
    cpu->OF = ((
             ((result)^(dest)) &
             ((result)^(src))
             )>>(7))&1;
    return result;
}

DWORD cpu_add16(vomit_cpu_t *cpu, WORD dest, WORD src)
{
    DWORD result = dest + src;
    vomit_cpu_math_flags16(cpu, result, dest, src);
    cpu->OF = ((
             ((result)^(dest)) &
             ((result)^(src))
             )>>(15))&1;
    return result;
}

WORD cpu_adc8(vomit_cpu_t *cpu, WORD dest, WORD src)
{
    WORD result;
    src += cpu->CF;
    result = dest + src;

    vomit_cpu_math_flags8(cpu, result, dest, src);
    cpu->OF = ((
             ((result)^(dest)) &
             ((result)^(src))
             )>>(7))&1;
    return result;
}

DWORD cpu_adc16(vomit_cpu_t *cpu, WORD dest, WORD src)
{
    DWORD result;
    src += cpu->CF;
    result = dest + src;

    vomit_cpu_math_flags16(cpu, result, dest, src);
    cpu->OF = ((
             ((result)^(dest)) &
             ((result)^(src))
             )>>(15))&1;
    return result;
}

WORD cpu_sub8(vomit_cpu_t *cpu, BYTE dest, BYTE src)
{
    WORD result = dest - src;
    vomit_cpu_cmp_flags8(cpu, result, dest, src);
    return result;
}

DWORD cpu_sub16(vomit_cpu_t *cpu, WORD dest, WORD src)
{
    DWORD result = dest - src;
    vomit_cpu_cmp_flags16(cpu, result, dest, src);
    return result;
}

WORD cpu_sbb8(vomit_cpu_t *cpu, BYTE dest, BYTE src)
{
    WORD result;
    src += cpu->CF;
    result = dest - src;
    vomit_cpu_cmp_flags8(cpu, result, dest, src);
    return result;
}

DWORD cpu_sbb16(vomit_cpu_t *cpu, WORD dest, WORD src)
{
    DWORD result;
    src += cpu->CF;
    result = dest - src;
    vomit_cpu_cmp_flags16(cpu, result, dest, src);
    return result;
}

WORD cpu_mul8(vomit_cpu_t *cpu, BYTE acc, BYTE multi)
{
    WORD result = acc * multi;
    vomit_cpu_math_flags8(cpu, result, acc, multi);

    /* 8086 CPUs set ZF on zero result */
    if (cpu->type == INTEL_8086)
        cpu->ZF = result == 0;

    return result;
}

DWORD cpu_mul16(vomit_cpu_t *cpu, WORD acc, WORD multi)
{
    DWORD result = acc * multi;
    vomit_cpu_math_flags16(cpu, result, acc, multi);

    /* 8086 CPUs set ZF on zero result */
    if (cpu->type == INTEL_8086)
        cpu->ZF = result == 0;

    return result;
}

SIGNED_WORD cpu_imul8(vomit_cpu_t *cpu, SIGNED_BYTE acc, SIGNED_BYTE multi)
{
    SIGNED_WORD result = acc * multi;
    vomit_cpu_math_flags8(cpu, result, acc, multi);
    return result;
}


SIGNED_DWORD cpu_imul16(vomit_cpu_t *cpu, SIGNED_WORD acc, SIGNED_WORD multi)
{
    SIGNED_DWORD result = acc * multi;
    vomit_cpu_math_flags16(cpu, result, acc, multi);
    return result;
}

DEFAULT_RM8_reg8( cpu_add, _ADD_RM8_reg8 )
DEFAULT_RM16_reg16( cpu_add, _ADD_RM16_reg16 )
DEFAULT_reg8_RM8( cpu_add, _ADD_reg8_RM8 )
DEFAULT_reg16_RM16( cpu_add, _ADD_reg16_RM16 )
DEFAULT_RM8_imm8( cpu_add, _ADD_RM8_imm8 )
DEFAULT_RM16_imm16( cpu_add, _ADD_RM16_imm16 )
DEFAULT_RM16_imm8( cpu_add, _ADD_RM16_imm8 )
DEFAULT_AL_imm8( cpu_add, _ADD_AL_imm8 )
DEFAULT_AX_imm16( cpu_add, _ADD_AX_imm16 )

DEFAULT_RM8_reg8( cpu_adc, _ADC_RM8_reg8 )
DEFAULT_RM16_reg16( cpu_adc, _ADC_RM16_reg16 )
DEFAULT_reg8_RM8( cpu_adc, _ADC_reg8_RM8 )
DEFAULT_reg16_RM16( cpu_adc, _ADC_reg16_RM16 )
DEFAULT_RM8_imm8( cpu_adc, _ADC_RM8_imm8 )
DEFAULT_RM16_imm16( cpu_adc, _ADC_RM16_imm16 )
DEFAULT_RM16_imm8( cpu_adc, _ADC_RM16_imm8 )
DEFAULT_AL_imm8( cpu_adc, _ADC_AL_imm8 )
DEFAULT_AX_imm16( cpu_adc, _ADC_AX_imm16 )

DEFAULT_RM8_reg8( cpu_sub, _SUB_RM8_reg8 )
DEFAULT_RM16_reg16( cpu_sub, _SUB_RM16_reg16 )
DEFAULT_reg8_RM8( cpu_sub, _SUB_reg8_RM8 )
DEFAULT_reg16_RM16( cpu_sub, _SUB_reg16_RM16 )
DEFAULT_RM8_imm8( cpu_sub, _SUB_RM8_imm8 )
DEFAULT_RM16_imm16( cpu_sub, _SUB_RM16_imm16 )
DEFAULT_RM16_imm8( cpu_sub, _SUB_RM16_imm8 )
DEFAULT_AL_imm8( cpu_sub, _SUB_AL_imm8 )
DEFAULT_AX_imm16( cpu_sub, _SUB_AX_imm16 )

DEFAULT_RM8_reg8( cpu_sbb, _SBB_RM8_reg8 )
DEFAULT_RM16_reg16( cpu_sbb, _SBB_RM16_reg16 )
DEFAULT_reg8_RM8( cpu_sbb, _SBB_reg8_RM8 )
DEFAULT_reg16_RM16( cpu_sbb, _SBB_reg16_RM16 )
DEFAULT_RM8_imm8( cpu_sbb, _SBB_RM8_imm8 )
DEFAULT_RM16_imm16( cpu_sbb, _SBB_RM16_imm16 )
DEFAULT_RM16_imm8( cpu_sbb, _SBB_RM16_imm8 )
DEFAULT_AL_imm8( cpu_sbb, _SBB_AL_imm8 )
DEFAULT_AX_imm16( cpu_sbb, _SBB_AX_imm16 )

READONLY_RM8_reg8( cpu_sub, _CMP_RM8_reg8 )
READONLY_RM16_reg16( cpu_sub, _CMP_RM16_reg16 )
READONLY_reg8_RM8( cpu_sub, _CMP_reg8_RM8 )
READONLY_reg16_RM16( cpu_sub, _CMP_reg16_RM16 )
READONLY_RM8_imm8( cpu_sub, _CMP_RM8_imm8 )
READONLY_RM16_imm16( cpu_sub, _CMP_RM16_imm16 )
READONLY_RM16_imm8( cpu_sub, _CMP_RM16_imm8 )
READONLY_AL_imm8( cpu_sub, _CMP_AL_imm8 )
READONLY_AX_imm16( cpu_sub, _CMP_AX_imm16 )

void _MUL_RM8(vomit_cpu_t *cpu)
{
    BYTE value = vomit_cpu_modrm_read8(cpu, cpu->rmbyte);
    cpu->regs.W.AX = cpu_mul8(cpu, cpu->regs.B.AL, value);

    if (cpu->regs.B.AH == 0x00) {
        cpu->CF = 0;
        cpu->OF = 0;
    } else {
        cpu->CF = 1;
        cpu->OF = 1;
    }
}

void _MUL_RM16(vomit_cpu_t *cpu)
{
    WORD value = vomit_cpu_modrm_read16(cpu, cpu->rmbyte);
    DWORD result = cpu_mul16(cpu, cpu->regs.W.AX, value);
    cpu->regs.W.AX = result & 0xFFFF;
    cpu->regs.W.DX = (result >> 16) & 0xFFFF;

    if (cpu->regs.W.DX == 0x0000) {
        cpu->CF = 0;
        cpu->OF = 0;
    } else {
        cpu->CF = 1;
        cpu->OF = 1;
    }
}

void _IMUL_RM8(vomit_cpu_t *cpu)
{
    SIGNED_BYTE value = (SIGNED_BYTE)vomit_cpu_modrm_read8(cpu, cpu->rmbyte);
    cpu->regs.W.AX = (SIGNED_WORD)cpu_imul8(cpu, cpu->regs.B.AL, value);

    if (cpu->regs.B.AH == 0x00 || cpu->regs.B.AH == 0xFF) {
        cpu->CF = 0;
        cpu->OF = 0;
    } else {
        cpu->CF = 1;
        cpu->OF = 1;
    }
}

void _IMUL_reg16_RM16_imm8(vomit_cpu_t *cpu)
{
    BYTE rm = vomit_cpu_pfq_getbyte(cpu);
    BYTE imm = vomit_cpu_pfq_getbyte(cpu);
    SIGNED_WORD value = (SIGNED_WORD)vomit_cpu_modrm_read16(cpu, rm);
    SIGNED_WORD result = cpu_imul16(cpu, value, imm);

    *cpu->treg16[rmreg(rm)] = result;

    if ((result & 0xFF00) == 0x00 || (result & 0xFF00) == 0xFF) {
        cpu->CF = 0;
        cpu->OF = 0;
    } else {
        cpu->CF = 1;
        cpu->OF = 1;
    }
}

void _IMUL_RM16(vomit_cpu_t *cpu)
{
    SIGNED_WORD value = vomit_cpu_modrm_read16(cpu, cpu->rmbyte);
    SIGNED_DWORD result = cpu_imul16(cpu, cpu->regs.W.AX, value);
    cpu->regs.W.AX = result;
    cpu->regs.W.DX = result >> 16;

    if (cpu->regs.W.DX == 0x0000 || cpu->regs.W.DX == 0xFFFF) {
        cpu->CF = 0;
        cpu->OF = 0;
    } else {
        cpu->CF = 1;
        cpu->OF = 1;
    }
}

void _DIV_RM8(vomit_cpu_t *cpu)
{
    WORD offend = cpu->IP;
    BYTE value = vomit_cpu_modrm_read8(cpu, cpu->rmbyte);
    WORD tAX = cpu->regs.W.AX;

    if (value == 0) {
        /* Exceptions return to offending IP */
        cpu->IP = offend - 2;
        vomit_cpu_isr_call(cpu, 0);
        return;
    }

    cpu->regs.B.AL = (byte)(tAX / value); /* Quote        */
    cpu->regs.B.AH = (byte)(tAX % value); /* Remainder    */
}

void _DIV_RM16(vomit_cpu_t *cpu)
{
    WORD offend = cpu->IP;
    WORD value = vomit_cpu_modrm_read16(cpu, cpu->rmbyte);
    DWORD tDXAX = cpu->regs.W.AX + (cpu->regs.W.DX << 16);

    if (value == 0) {
        /* Exceptions return to offending IP */
        cpu->IP = offend - 2;
        vomit_cpu_isr_call(cpu, 0);
        return;
    }

    cpu->regs.W.AX = (WORD)(tDXAX / value); /* Quote      */
    cpu->regs.W.DX = (WORD)(tDXAX % value); /* Remainder  */
}

void _IDIV_RM8(vomit_cpu_t *cpu)
{
    WORD offend = cpu->IP;
    SIGNED_BYTE value = (SIGNED_BYTE)vomit_cpu_modrm_read8(cpu, cpu->rmbyte);
    SIGNED_WORD tAX = (SIGNED_WORD)cpu->regs.W.AX;

    if (value == 0) {
        /* Exceptions return to offending IP */
        cpu->IP = offend - 2;
        vomit_cpu_isr_call(cpu, 0);
        return;
    }

    cpu->regs.B.AL = (SIGNED_BYTE)(tAX / value); /* Quote        */
    cpu->regs.B.AH = (SIGNED_BYTE)(tAX % value); /* Remainder    */
}

void _IDIV_RM16(vomit_cpu_t *cpu)
{
    WORD offend = cpu->IP;
    SIGNED_WORD value = vomit_cpu_modrm_read16(cpu, cpu->rmbyte);
    SIGNED_DWORD tDXAX = (cpu->regs.W.AX + (cpu->regs.W.DX << 16));

    if (value == 0) {
        /* Exceptions return to offending IP */
        cpu->IP = offend - 2;
        vomit_cpu_isr_call(cpu, 0);
        return;
    }
    cpu->regs.W.AX = (SIGNED_WORD)(tDXAX / value); /* Quote      */
    cpu->regs.W.DX = (SIGNED_WORD)(tDXAX % value); /* Remainder  */
}
