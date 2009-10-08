#include "vomit.h"
#include "debug.h"
#include <stdlib.h>

#define DEFAULT_TO_SS if (cpu->CurrentSegment != &cpu->SegmentPrefix ) { segment = cpu->SS; }
static void *s_last_modrm_ptr = 0L;
static int s_last_is_register = 0;

static word s_last_modrm_segment = 0;
static word s_last_modrm_offset = 0;

void vomit_cpu_modrm_write16(vomit_cpu_t *cpu, BYTE rmbyte, WORD value)
{
    BYTE *rmp = (BYTE *)vomit_cpu_modrm_resolve16(cpu, rmbyte);
    if (MODRM_ISREG(rmbyte)) {
        *((word *)rmp) = value;
    } else {
        vomit_cpu_memory_write16(cpu, s_last_modrm_segment, s_last_modrm_offset, value);
    }
}

void vomit_cpu_modrm_write8(vomit_cpu_t *cpu, BYTE rmbyte, BYTE value)
{
    BYTE *rmp = (BYTE *)vomit_cpu_modrm_resolve8(cpu, rmbyte);
    if (MODRM_ISREG(rmbyte)) {
        *rmp = value;
    } else {
        vomit_cpu_memory_write8(cpu, s_last_modrm_segment, s_last_modrm_offset, value);
    }
}

WORD vomit_cpu_modrm_read16(vomit_cpu_t *cpu, BYTE rmbyte)
{
    BYTE *rmp = (BYTE *)vomit_cpu_modrm_resolve16(cpu, rmbyte);
    if (MODRM_ISREG(rmbyte))
        return *((word *)rmp);
    return vomit_cpu_memory_read16(cpu, s_last_modrm_segment, s_last_modrm_offset);
}

BYTE vomit_cpu_modrm_read8(vomit_cpu_t *cpu, BYTE rmbyte)
{
    BYTE *rmp = (BYTE *)vomit_cpu_modrm_resolve8(cpu, rmbyte);
    if (MODRM_ISREG(rmbyte))
        return *rmp;
    return vomit_cpu_memory_read8(cpu, s_last_modrm_segment, s_last_modrm_offset);
}

void vomit_cpu_modrm_update16(vomit_cpu_t *cpu, WORD value)
{
    if (s_last_is_register) {
        *((WORD *)s_last_modrm_ptr) = value;
    } else {
        vomit_cpu_memory_write16(cpu, s_last_modrm_segment, s_last_modrm_offset, value);
    }
}

void vomit_cpu_modrm_update8(vomit_cpu_t *cpu, BYTE value)
{
    if (s_last_is_register) {
        *((BYTE *)s_last_modrm_ptr) = value;
    } else {
        vomit_cpu_memory_write8(cpu, s_last_modrm_segment, s_last_modrm_offset, value);
    }
}

DWORD vomit_cpu_modrm_read32(vomit_cpu_t *cpu, byte rmbyte)
{
    /* NOTE: We don't need modrm_resolve32() at the moment. */
    BYTE *rmp = (BYTE *)vomit_cpu_modrm_resolve8(cpu, rmbyte);

    if (MODRM_ISREG(rmbyte)) {
        vlog(VM_CPUMSG, "PANIC: Attempt to read 32-bit register.");
        vm_exit(1);
    }

    return rmp[0] | (rmp[1]<<8) | (rmp[2]<<16) | (rmp[3]<<24);
}

void _LEA_reg16_mem16(vomit_cpu_t *cpu)
{
    WORD retv = 0x0000;
    BYTE b = vomit_cpu_pfq_getbyte(cpu);
    switch (b & 0xC0) {
        case 0:
            switch(b & 0x07)
            {
                case 0: retv = cpu->regs.W.BX+cpu->regs.W.SI; break;
                case 1: retv = cpu->regs.W.BX+cpu->regs.W.DI; break;
                case 2: retv = cpu->regs.W.BP+cpu->regs.W.SI; break;
                case 3: retv = cpu->regs.W.BP+cpu->regs.W.DI; break;
                case 4: retv = cpu->regs.W.SI; break;
                case 5: retv = cpu->regs.W.DI; break;
                case 6: retv = vomit_cpu_pfq_getword(cpu); break;
                default: retv = cpu->regs.W.BX; break;
            }
            break;
        case 64:
            switch(b & 0x07)
            {
                case 0: retv = cpu->regs.W.BX+cpu->regs.W.SI + signext(vomit_cpu_pfq_getbyte(cpu)); break;
                case 1: retv = cpu->regs.W.BX+cpu->regs.W.DI + signext(vomit_cpu_pfq_getbyte(cpu)); break;
                case 2: retv = cpu->regs.W.BP+cpu->regs.W.SI + signext(vomit_cpu_pfq_getbyte(cpu)); break;
                case 3: retv = cpu->regs.W.BP+cpu->regs.W.DI + signext(vomit_cpu_pfq_getbyte(cpu)); break;
                case 4: retv = cpu->regs.W.SI + signext(vomit_cpu_pfq_getbyte(cpu)); break;
                case 5: retv = cpu->regs.W.DI + signext(vomit_cpu_pfq_getbyte(cpu)); break;
                case 6: retv = cpu->regs.W.BP + signext(vomit_cpu_pfq_getbyte(cpu)); break;
                default: retv = cpu->regs.W.BX + signext(vomit_cpu_pfq_getbyte(cpu)); break;
            }
            break;
        case 128:
            switch(b & 0x07)
            {
                case 0: retv = cpu->regs.W.BX+cpu->regs.W.SI+vomit_cpu_pfq_getword(cpu); break;
                case 1: retv = cpu->regs.W.BX+cpu->regs.W.DI+vomit_cpu_pfq_getword(cpu); break;
                case 2: retv = cpu->regs.W.BP+cpu->regs.W.SI+vomit_cpu_pfq_getword(cpu); break;
                case 3: retv = cpu->regs.W.BP+cpu->regs.W.DI+vomit_cpu_pfq_getword(cpu); break;
                case 4: retv = cpu->regs.W.SI + vomit_cpu_pfq_getword(cpu); break;
                case 5: retv = cpu->regs.W.DI + vomit_cpu_pfq_getword(cpu); break;
                case 6: retv = cpu->regs.W.BP + vomit_cpu_pfq_getword(cpu); break;
                default: retv = cpu->regs.W.BX + vomit_cpu_pfq_getword(cpu); break;
            }
            break;
        case 192:
            vlog(VM_ALERT, "LEA with register source!");
            /* LEA with register source, an invalid instruction.
             * Call INT6 (invalid opcode exception) */
            vomit_cpu_isr_call(cpu, 6);
            break;
    }
    *cpu->treg16[rmreg(b)] = retv;
}

void *vomit_cpu_modrm_resolve8(vomit_cpu_t *cpu, BYTE rmbyte)
{
    WORD segment = *cpu->CurrentSegment;
    WORD offset = 0x0000;

    switch (rmbyte & 0xC0) {
        case 0x00:
            s_last_is_register = 0;
            switch (rmbyte & 0x07) {
                case 0: offset = cpu->regs.W.BX + cpu->regs.W.SI; break;
                case 1: offset = cpu->regs.W.BX + cpu->regs.W.DI; break;
                case 2: DEFAULT_TO_SS; offset = cpu->regs.W.BP + cpu->regs.W.SI; break;
                case 3: DEFAULT_TO_SS; offset = cpu->regs.W.BP + cpu->regs.W.DI; break;
                case 4: offset = cpu->regs.W.SI; break;
                case 5: offset = cpu->regs.W.DI; break;
                case 6: offset = vomit_cpu_pfq_getword(cpu); break;
                default: offset = cpu->regs.W.BX; break;
            }
            s_last_modrm_segment = segment;
            s_last_modrm_offset = offset;
            s_last_modrm_ptr = &cpu->memory[(segment<<4) + offset];
            break;
        case 0x40:
            s_last_is_register = 0;
            offset = signext( vomit_cpu_pfq_getbyte(cpu) );
            switch (rmbyte & 0x07) {
                case 0: offset += cpu->regs.W.BX + cpu->regs.W.SI; break;
                case 1: offset += cpu->regs.W.BX + cpu->regs.W.DI; break;
                case 2: DEFAULT_TO_SS; offset += cpu->regs.W.BP + cpu->regs.W.SI; break;
                case 3: DEFAULT_TO_SS; offset += cpu->regs.W.BP + cpu->regs.W.DI; break;
                case 4: offset += cpu->regs.W.SI; break;
                case 5: offset += cpu->regs.W.DI; break;
                case 6: DEFAULT_TO_SS; offset += cpu->regs.W.BP; break;
                default: offset += cpu->regs.W.BX; break;
            }
            s_last_modrm_segment = segment;
            s_last_modrm_offset = offset;
            s_last_modrm_ptr = &cpu->memory[(segment<<4) + offset];
            break;
        case 0x80:
            s_last_is_register = 0;
            offset = vomit_cpu_pfq_getword(cpu);
            switch (rmbyte & 0x07) {
                case 0: offset += cpu->regs.W.BX + cpu->regs.W.SI; break;
                case 1: offset += cpu->regs.W.BX + cpu->regs.W.DI; break;
                case 2: DEFAULT_TO_SS; offset += cpu->regs.W.BP + cpu->regs.W.SI; break;
                case 3: DEFAULT_TO_SS; offset += cpu->regs.W.BP + cpu->regs.W.DI; break;
                case 4: offset += cpu->regs.W.SI; break;
                case 5: offset += cpu->regs.W.DI; break;
                case 6: DEFAULT_TO_SS; offset += cpu->regs.W.BP; break;
                default: offset += cpu->regs.W.BX; break;
            }
            s_last_modrm_segment = segment;
            s_last_modrm_offset = offset;
            s_last_modrm_ptr = &cpu->memory[(segment<<4) + offset];
            break;
        case 0xC0:
            s_last_is_register = 1;
            switch (rmbyte & 0x07) {
                case 0: s_last_modrm_ptr = &cpu->regs.B.AL; break;
                case 1: s_last_modrm_ptr = &cpu->regs.B.CL; break;
                case 2: s_last_modrm_ptr = &cpu->regs.B.DL; break;
                case 3: s_last_modrm_ptr = &cpu->regs.B.BL; break;
                case 4: s_last_modrm_ptr = &cpu->regs.B.AH; break;
                case 5: s_last_modrm_ptr = &cpu->regs.B.CH; break;
                case 6: s_last_modrm_ptr = &cpu->regs.B.DH; break;
                default: s_last_modrm_ptr = &cpu->regs.B.BH; break;
            }
            break;
    }
    return s_last_modrm_ptr;
}

void * vomit_cpu_modrm_resolve16(vomit_cpu_t *cpu, BYTE rmbyte)
{
    WORD segment = *cpu->CurrentSegment;
    WORD offset = 0x0000;

    switch (rmbyte & 0xC0) {
        case 0x00:
            s_last_is_register = 0;
            switch (rmbyte & 0x07) {
                case 0: offset = cpu->regs.W.BX + cpu->regs.W.SI; break;
                case 1: offset = cpu->regs.W.BX + cpu->regs.W.DI; break;
                case 2: DEFAULT_TO_SS; offset = cpu->regs.W.BP + cpu->regs.W.SI; break;
                case 3: DEFAULT_TO_SS; offset = cpu->regs.W.BP + cpu->regs.W.DI; break;
                case 4: offset = cpu->regs.W.SI; break;
                case 5: offset = cpu->regs.W.DI; break;
                case 6: offset = vomit_cpu_pfq_getword(cpu); break;
                default: offset = cpu->regs.W.BX; break;
            }
            s_last_modrm_segment = segment;
            s_last_modrm_offset = offset;
            s_last_modrm_ptr = &cpu->memory[(segment<<4) + offset];
            break;
        case 0x40:
            s_last_is_register = 0;
            offset = signext( vomit_cpu_pfq_getbyte(cpu) );
            switch (rmbyte & 0x07) {
                case 0: offset += cpu->regs.W.BX + cpu->regs.W.SI; break;
                case 1: offset += cpu->regs.W.BX + cpu->regs.W.DI; break;
                case 2: DEFAULT_TO_SS; offset += cpu->regs.W.BP + cpu->regs.W.SI; break;
                case 3: DEFAULT_TO_SS; offset += cpu->regs.W.BP + cpu->regs.W.DI; break;
                case 4: offset += cpu->regs.W.SI; break;
                case 5: offset += cpu->regs.W.DI; break;
                case 6: DEFAULT_TO_SS; offset += cpu->regs.W.BP; break;
                default: offset += cpu->regs.W.BX; break;
            }
            s_last_modrm_segment = segment;
            s_last_modrm_offset = offset;
            s_last_modrm_ptr = &cpu->memory[(segment<<4) + offset];
            break;
        case 0x80:
            s_last_is_register = 0;
            offset = vomit_cpu_pfq_getword(cpu);
            switch (rmbyte & 0x07) {
                case 0: offset += cpu->regs.W.BX + cpu->regs.W.SI; break;
                case 1: offset += cpu->regs.W.BX + cpu->regs.W.DI; break;
                case 2: DEFAULT_TO_SS; offset += cpu->regs.W.BP + cpu->regs.W.SI; break;
                case 3: DEFAULT_TO_SS; offset += cpu->regs.W.BP + cpu->regs.W.DI; break;
                case 4: offset += cpu->regs.W.SI; break;
                case 5: offset += cpu->regs.W.DI; break;
                case 6: DEFAULT_TO_SS; offset += cpu->regs.W.BP; break;
                default: offset += cpu->regs.W.BX; break;
            }
            s_last_modrm_segment = segment;
            s_last_modrm_offset = offset;
            s_last_modrm_ptr = &cpu->memory[(segment<<4) + offset];
            break;
        case 0xC0:
            s_last_is_register = 1;
            switch (rmbyte & 0x07) {
                case 0: s_last_modrm_ptr = &cpu->regs.W.AX; break;
                case 1: s_last_modrm_ptr = &cpu->regs.W.CX; break;
                case 2: s_last_modrm_ptr = &cpu->regs.W.DX; break;
                case 3: s_last_modrm_ptr = &cpu->regs.W.BX; break;
                case 4: s_last_modrm_ptr = &cpu->regs.W.SP; break;
                case 5: s_last_modrm_ptr = &cpu->regs.W.BP; break;
                case 6: s_last_modrm_ptr = &cpu->regs.W.SI; break;
                default: s_last_modrm_ptr = &cpu->regs.W.DI; break;
            }
            break;
    }
    return s_last_modrm_ptr;
}

