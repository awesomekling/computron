/*
 * Copyright (C) 2003-2011 Andreas Kling <kling@webkit.org>
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

#include "Common.h"
#include "debug.h"
#include <string.h>

#define DMA_CHANNELS 8

struct dma_channel_t {
    WORD count;
    bool next_count_write_is_msb;
    bool next_count_read_is_msb;

    WORD address;
    bool next_address_write_is_msb;
    bool next_address_read_is_msb;
};

static dma_channel_t dma1_channel[DMA_CHANNELS];
static dma_channel_t dma2_channel[DMA_CHANNELS];

static void dma_ch_count_write(VCpu* cpu, WORD port, BYTE data);
static BYTE dma_ch_count_read(VCpu* cpu, WORD port);
static void dma_ch_address_write(VCpu* cpu, WORD port, BYTE data);
static BYTE dma_ch_address_read(VCpu* cpu, WORD port);

void dma_init()
{
    for (int i = 0; i < DMA_CHANNELS; ++i) {
        memset(&dma1_channel[i], 0, sizeof(dma_channel_t));
        memset(&dma2_channel[i], 0, sizeof(dma_channel_t));
    }

    vm_listen(0x000, dma_ch_address_read, dma_ch_address_write);
    vm_listen(0x001, dma_ch_count_read, dma_ch_count_write);
    vm_listen(0x002, dma_ch_address_read, dma_ch_address_write);
    vm_listen(0x003, dma_ch_count_read, dma_ch_count_write);
    vm_listen(0x004, dma_ch_address_read, dma_ch_address_write);
    vm_listen(0x005, dma_ch_count_read, dma_ch_count_write);
    vm_listen(0x006, dma_ch_address_read, dma_ch_address_write);
    vm_listen(0x007, dma_ch_count_read, dma_ch_count_write);
}

void dma_ch_count_write(VCpu*, WORD port, BYTE data)
{
    dma_channel_t* c;
    BYTE channel, chip = 0;

    switch (port) {
    case 0x001:
        channel = 0;
        break;
    case 0x003:
        channel = 1;
        break;
    case 0x005:
        channel = 2;
        break;
    case 0x007:
        channel = 3;
        break;
    default:
        vlog(VM_DMAMSG, "Unknown channel for port %03X", port);
        return;
    }

    c = &dma1_channel[channel];

    if (c->next_count_write_is_msb)
        c->count |= (data << 8);
    else
        c->count |= (data & 0xFF);

    vlog(VM_DMAMSG, "Write DMA-%u channel %u count (%s), data: %02X", chip, channel, c->next_count_write_is_msb ? "MSB" : "LSB", data);

    c->next_count_write_is_msb = !c->next_count_write_is_msb;
}

BYTE dma_ch_count_read(VCpu*, WORD port)
{
    dma_channel_t* c;
    BYTE data;
    BYTE channel, chip;

    if (port == 0x001) {
        channel = 0;
        chip = 1;
    } else
        vlog(VM_DMAMSG, "Unknown channel for port %03X", port);

    c = (chip == 1) ? &dma1_channel[channel] : &dma2_channel[channel];

    if (c->next_count_read_is_msb)
        data = c->count >> 8;
    else
        data = c->count & 0xFF;

    vlog(VM_DMAMSG, "Read DMA-%u channel %u count (%s), data: %02X", chip, channel, c->next_count_read_is_msb ? "MSB" : "LSB", data);

    c->next_count_read_is_msb = !c->next_count_read_is_msb;
    return data;
}

void dma_ch_address_write(VCpu*, WORD port, BYTE data)
{
    dma_channel_t* c;
    BYTE channel, chip = 0;

    switch (port) {
    case 0x000:
        channel = 0;
        break;
    case 0x002:
        channel = 1;
        break;
    case 0x004:
        channel = 2;
        break;
    case 0x006:
        channel = 3;
        break;
    default:
        vlog(VM_DMAMSG, "Unknown channel for port %03X", port);
        return;
    }

    c = &dma1_channel[channel];

    if (c->next_address_write_is_msb)
        c->address |= data << 8;
    else
        c->address |= data & 0xFF;

    vlog(VM_DMAMSG, "Write DMA-%u channel %u address (%s), data: %02X", chip, channel, c->next_address_write_is_msb ? "MSB" : "LSB", data);

    c->next_address_write_is_msb = !c->next_address_write_is_msb;
}

BYTE dma_ch_address_read(VCpu*, WORD port)
{
    dma_channel_t* c;
    BYTE data;
    BYTE channel, chip = 0;

    switch (port) {
    case 0x000:
        channel = 0;
        break;
    case 0x002:
        channel = 1;
        break;
    case 0x004:
        channel = 2;
        break;
    case 0x006:
        channel = 3;
        break;
    default:
        vlog(VM_DMAMSG, "Unknown channel for port %03X", port);
        return 0;
    }

    c = &dma1_channel[channel];

    if (c->next_address_read_is_msb)
        data = c->address >> 8;
    else
        data = c->address & 0xFF;

    vlog(VM_DMAMSG, "Read DMA-%u channel %u address (%s), data: %02X", chip, channel, c->next_address_read_is_msb ? "MSB" : "LSB", data);

    c->next_address_read_is_msb = !c->next_address_read_is_msb;
    return 1;
    return data;
}
