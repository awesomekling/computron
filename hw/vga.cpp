// Computron x86 PC Emulator
// Copyright (C) 2003-2019 Andreas Kling <awesomekling@gmail.com>
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

#include "Common.h"
#include "vga.h"
#include "debug.h"
#include "machine.h"
#include "CPU.h"
#include <QtGui/QColor>
#include <QtGui/QBrush>

struct RGBColor {
    BYTE red;
    BYTE green;
    BYTE blue;
    operator QColor() const { return QColor::fromRgb(red << 2, green << 2, blue << 2); }
};

struct VGA::Private
{
    QColor color[16];
    QBrush brush[16];
    BYTE* memory { nullptr };
    BYTE* plane[4];
    BYTE latch[4];

    struct {
        BYTE reg_index;
        BYTE reg[0x19];
    } crtc;

    struct {
        bool next_3c0_is_index;
        bool palette_address_source { false };
        BYTE reg_index;
        BYTE palette_reg[0x10];
        BYTE mode_control;
        BYTE overscan_color;
        BYTE color_plane_enable;
        BYTE horizontal_pixel_panning;
        BYTE color_select;
    } attr;

    struct {
        BYTE reg_index;
        BYTE reg[5];
    } sequencer;

    struct {
        BYTE reg_index;
        BYTE reg[9];
        BYTE memory_map_select;
    } graphics_ctrl;

    struct {
        bool vertical_sync_polarity;
        bool horizontal_sync_polarity;
        bool odd_even_page_select;
        BYTE clock_select;
        bool ram_enable;
        bool input_output_address_select;
    } misc_output;

    struct {
        BYTE data_read_index;
        BYTE data_read_subindex;
        BYTE data_write_index;
        BYTE data_write_subindex;
        RGBColor color[256];
        BYTE mask;
    } dac;

    BYTE columns;
    BYTE rows;

    bool vga_enabled;

    bool paletteDirty { true };

    bool write_protect;

    bool screenInRefresh { false };
    BYTE statusRegister { 0 };
};

static const RGBColor default_vga_color_registers[256] =
{
    {0x00,0x00,0x00}, {0x00,0x00,0x2a}, {0x00,0x2a,0x00}, {0x00,0x2a,0x2a}, {0x2a,0x00,0x00}, {0x2a,0x00,0x2a}, {0x2a,0x15,0x00}, {0x2a,0x2a,0x2a},
    {0x00,0x00,0x00}, {0x00,0x00,0x2a}, {0x00,0x2a,0x00}, {0x00,0x2a,0x2a}, {0x2a,0x00,0x00}, {0x2a,0x00,0x2a}, {0x2a,0x15,0x00}, {0x2a,0x2a,0x2a},
    {0x15,0x15,0x15}, {0x15,0x15,0x3f}, {0x15,0x3f,0x15}, {0x15,0x3f,0x3f}, {0x3f,0x15,0x15}, {0x3f,0x15,0x3f}, {0x3f,0x3f,0x15}, {0x3f,0x3f,0x3f},
    {0x15,0x15,0x15}, {0x15,0x15,0x3f}, {0x15,0x3f,0x15}, {0x15,0x3f,0x3f}, {0x3f,0x15,0x15}, {0x3f,0x15,0x3f}, {0x3f,0x3f,0x15}, {0x3f,0x3f,0x3f},
    {0x00,0x00,0x00}, {0x00,0x00,0x2a}, {0x00,0x2a,0x00}, {0x00,0x2a,0x2a}, {0x2a,0x00,0x00}, {0x2a,0x00,0x2a}, {0x2a,0x15,0x00}, {0x2a,0x2a,0x2a},
    {0x00,0x00,0x00}, {0x00,0x00,0x2a}, {0x00,0x2a,0x00}, {0x00,0x2a,0x2a}, {0x2a,0x00,0x00}, {0x2a,0x00,0x2a}, {0x2a,0x15,0x00}, {0x2a,0x2a,0x2a},
    {0x15,0x15,0x15}, {0x15,0x15,0x3f}, {0x15,0x3f,0x15}, {0x15,0x3f,0x3f}, {0x3f,0x15,0x15}, {0x3f,0x15,0x3f}, {0x3f,0x3f,0x15}, {0x3f,0x3f,0x3f},
    {0x15,0x15,0x15}, {0x15,0x15,0x3f}, {0x15,0x3f,0x15}, {0x15,0x3f,0x3f}, {0x3f,0x15,0x15}, {0x3f,0x15,0x3f}, {0x3f,0x3f,0x15}, {0x3f,0x3f,0x3f},
};

const BYTE* VGA::text_memory() const
{
    return d->memory;
}

VGA::VGA(Machine& m)
    : IODevice("VGA", m)
    , MemoryProvider(PhysicalAddress(0xa0000), 131072)
    , d(make<Private>())
{
    machine().cpu().registerMemoryProvider(*this);

    listen(0x3B4, IODevice::ReadWrite);
    listen(0x3B5, IODevice::ReadWrite);
    listen(0x3BA, IODevice::ReadWrite);

    for (WORD port = 0x3c0; port <= 0x3cf; ++port)
        listen(port, IODevice::ReadWrite);

    listen(0x3D4, IODevice::ReadWrite);
    listen(0x3D5, IODevice::ReadWrite);
    listen(0x3DA, IODevice::ReadWrite);

    reset();
}

VGA::~VGA()
{
    delete [] d->memory;
}

void VGA::reset()
{
    d->columns = 80;
    d->rows = 0;

    d->crtc.reg_index = 0;
    d->graphics_ctrl.reg_index = 0;
    d->sequencer.reg_index = 0;
    d->attr.reg_index = 0;

    memset(d->crtc.reg, 0, sizeof(d->crtc.reg));
    memset(d->graphics_ctrl.reg, 0, sizeof(d->graphics_ctrl.reg));
    memset(d->sequencer.reg, 0, sizeof(d->sequencer.reg));

    d->graphics_ctrl.memory_map_select = 2;

    d->sequencer.reg[2] = 0x0F;

    d->crtc.reg[0x13] = 80;

    d->dac.data_read_index = 0;
    d->dac.data_read_subindex = 0;
    d->dac.data_write_index = 0;
    d->dac.data_write_subindex = 0;
    d->dac.mask = 0xff;
    d->vga_enabled = true;

    d->misc_output.vertical_sync_polarity = 1;
    d->misc_output.horizontal_sync_polarity = 1;
    d->misc_output.odd_even_page_select = 0;
    d->misc_output.clock_select = 0;
    d->misc_output.ram_enable = 1;
    d->misc_output.input_output_address_select = 1;

    for (int i = 0; i < 16; ++i)
        d->attr.palette_reg[i] = i;

    d->attr.next_3c0_is_index = true;
    d->attr.palette_address_source = true;
    // FIXME: Find the correct post-reset values for these registers.
    d->attr.mode_control = 3;
    d->attr.overscan_color = 0;
    d->attr.color_plane_enable = 0;
    d->attr.horizontal_pixel_panning = 0;
    d->attr.color_select = 0;

    memcpy(d->dac.color, default_vga_color_registers, sizeof(default_vga_color_registers));

    d->paletteDirty = true;
    d->screenInRefresh = false;
    d->statusRegister = 0;

    d->memory = new BYTE[0x40000];
    d->plane[0] = d->memory;
    d->plane[1] = d->plane[0] + 0x10000;
    d->plane[2] = d->plane[1] + 0x10000;
    d->plane[3] = d->plane[2] + 0x10000;

    memset(d->memory, 0x00, 0x40000);

    d->latch[0] = 0;
    d->latch[1] = 0;
    d->latch[2] = 0;
    d->latch[3] = 0;

    d->write_protect = false;

    synchronizeColors();
    setPaletteDirty(true);
}

void VGA::out8(WORD port, BYTE data)
{
    machine().notifyScreen();

    switch (port) {
    case 0x3B4:
    case 0x3D4:
        d->crtc.reg_index = data & 0x3f;
        if (d->crtc.reg_index > 0x18)
            vlog(LogVGA, "Invalid I/O register 0x%02X selected through port %03X", d->crtc.reg_index, port);
        else if (options.vgadebug)
            vlog(LogVGA, "I/O register 0x%02X selected through port %03X", d->crtc.reg_index, port);
        break;

    case 0x3B5:
    case 0x3D5:
        if (d->crtc.reg_index > 0x18) {
            vlog(LogVGA, "Invalid I/O register 0x%02X written (%02X) through port %03X", d->crtc.reg_index, data, port);
            ASSERT_NOT_REACHED();
            break;
        }
        if (options.vgadebug)
            vlog(LogVGA, "I/O register 0x%02X written (%02X) through port %03X", d->crtc.reg_index, data, port);
        if (d->write_protect && d->crtc.reg_index < 8) {
            if (d->crtc.reg_index == 7) {
                d->crtc.reg[d->crtc.reg_index] &= ~0x10;
                d->crtc.reg[d->crtc.reg_index] |= data & 0x10;
            }
        }
        if (d->crtc.reg_index == 0x11) {
            d->write_protect = data & 0x80;
            vlog(LogVGA, "write_protect <- %u", d->write_protect);
            //ASSERT_NOT_REACHED();
        }
        d->crtc.reg[d->crtc.reg_index] = data;
        break;

    case 0x3BA:
        vlog(LogVGA, "Writing FCR");
        break;

    case 0x3c2:
        vlog(LogVGA, "Writing MOR (Miscellaneous Output Register), data: %02x", data);
        d->misc_output.input_output_address_select = (data >> 0) & 1;
        d->misc_output.ram_enable = (data >> 1) & 1;
        d->misc_output.clock_select = (data >> 2) & 3;
        d->misc_output.odd_even_page_select = (data >> 5) & 1;
        d->misc_output.horizontal_sync_polarity = (data >> 6) & 1;
        d->misc_output.vertical_sync_polarity = (data >> 7) & 1;
        // FIXME: Support remapping between 3bx/3dx
        ASSERT(d->misc_output.input_output_address_select == true);
        break;

    case 0x3C0: {
        if (d->attr.next_3c0_is_index) {
            d->attr.reg_index = data & 0x1f;
            d->attr.palette_address_source = data & 0x20;
        } else {
            if (d->attr.reg_index < 0x10) {
                d->attr.palette_reg[d->attr.reg_index] = data;
            } else {
                switch (d->attr.reg_index) {
                case 0x10:
                    d->attr.mode_control = data;
                    break;
                case 0x11:
                    d->attr.overscan_color = data & 0x3f;
                    break;
                case 0x12:
                    d->attr.color_plane_enable = data;
                    break;
                case 0x13:
                    d->attr.horizontal_pixel_panning = data & 0xf;
                    break;
                case 0x14:
                    d->attr.color_select = data & 0xf;
                    break;
                default:
                    vlog(LogVGA, "3c0 unhandled write to attribute register %02x", d->attr.reg_index);
                    break;
                }
            }
        }
        d->attr.next_3c0_is_index = !d->attr.next_3c0_is_index;
        break;
    }

    case 0x3C3:
        d->vga_enabled = data & 1;
        break;

    case 0x3C4:
        d->sequencer.reg_index = data & 0x1F;
        if (d->sequencer.reg_index > 4)
            vlog(LogVGA, "Invalid VGA sequencer register #%u selected", d->sequencer.reg_index);
        break;

    case 0x3C5:
        if (d->sequencer.reg_index > 4) {
            vlog(LogVGA, "Invalid VGA sequencer register #%u written (data: %02x)", d->sequencer.reg_index, data);
            break;
        }
        d->sequencer.reg[d->sequencer.reg_index] = data;
        break;

    case 0x3C6:
        d->dac.mask = data;
        break;

    case 0x3C7:
        d->dac.data_read_index = data;
        d->dac.data_read_subindex = 0;
        break;

    case 0x3C8:
        d->dac.data_write_index = data;
        d->dac.data_write_subindex = 0;
        break;

    case 0x3C9: {
        // vlog(LogVGA, "Setting component %u of color %02X to %02X", dac_data_subindex, dac_data_index, data);
        RGBColor& color = d->dac.color[d->dac.data_write_index];
        switch (d->dac.data_write_subindex) {
        case 0:
            color.red = data;
            d->dac.data_write_subindex = 1;
            break;
        case 1:
            color.green = data;
            d->dac.data_write_subindex = 2;
            break;
        case 2:
            color.blue = data;
            d->dac.data_write_subindex = 0;
            d->dac.data_write_index += 1;
            break;
        }

        setPaletteDirty(true);
        break;
    }

    case 0x3ce:
        if (data > 8) {
            vlog(LogVGA, "Selecting invalid graphics register %u", data);
            //ASSERT_NOT_REACHED();
        }
        d->graphics_ctrl.reg_index = data;
        break;

    case 0x3cf:
        if (d->graphics_ctrl.reg_index > 8) {
            vlog(LogVGA, "Write to invalid graphics register %u <- %02x", d->graphics_ctrl.reg_index, data);
            break;
        }
        d->graphics_ctrl.reg[d->graphics_ctrl.reg_index] = data;
        if (d->graphics_ctrl.reg_index == 0x6) {
            d->graphics_ctrl.memory_map_select = (data >> 2) & 3;
            vlog(LogVGA, "Memory map select: %u", d->graphics_ctrl.memory_map_select);
        }
        break;

    default:
        vlog(LogVGA, "Unhandled VGA write %04x <- %02x", port, data);
        ASSERT_NOT_REACHED();
        IODevice::out8(port, data);
    }
}

void VGA::willRefreshScreen()
{
    d->screenInRefresh = true;
}

void VGA::didRefreshScreen()
{
    d->screenInRefresh = false;
    d->statusRegister |= 0x08;
}

BYTE VGA::in8(WORD port)
{
    switch (port) {
    case 0x3C0:
        if (d->attr.next_3c0_is_index)
            return d->attr.reg_index | (d->attr.palette_address_source * 0x20);
        vlog(LogVGA, "Port 3c0 read in unexpected mode!");
        return 0;

    case 0x3c2:
    case 0x3cd:
        return 0;

    case 0x3C3:
        return d->vga_enabled;

    case 0x3C6:
        return d->dac.mask;

    case 0x3B4:
        ASSERT_NOT_REACHED();
        return 0;

    case 0x3D4:
        return d->crtc.reg_index;

    case 0x3B5:
    case 0x3D5:
        if (d->crtc.reg_index > 0x18) {
            vlog(LogVGA, "Invalid I/O register 0x%02X read through port %03X", d->crtc.reg_index, port);
            return 0;
        }
        if (options.vgadebug)
            vlog(LogVGA, "I/O register 0x%02X read through port %03X", d->crtc.reg_index, port);
        return d->crtc.reg[d->crtc.reg_index];

    case 0x3BA:
    case 0x3DA: {
        BYTE value = d->statusRegister;
        // 6845 - Port 3DA Status Register
        //
        //  |7|6|5|4|3|2|1|0|  3DA Status Register
        //  | | | | | | | `---- 1 = display enable, RAM access is OK
        //  | | | | | | `----- 1 = light pen trigger set
        //  | | | | | `------ 0 = light pen on, 1 = light pen off
        //  | | | | `------- 1 = vertical retrace, RAM access OK for next 1.25ms
        //  `-------------- unused

        d->statusRegister ^= 0x01;
        d->statusRegister &= 0x01;

        d->attr.next_3c0_is_index = true;
        return value;
    }

    case 0x3C1:
        //vlog(LogVGA, "Read PALETTE[%u] (=%02X)", d->attr.reg_index, d->attr.palette_reg[d->paletteIndex]);
        if (d->attr.reg_index < 0x10)
            return d->attr.palette_reg[d->attr.reg_index];
        switch (d->attr.reg_index) {
        case 0x10:
            return d->attr.mode_control;
        case 0x11:
            return d->attr.overscan_color;
        case 0x12:
            return d->attr.color_plane_enable;
        case 0x13:
            return d->attr.horizontal_pixel_panning;
        case 0x14:
            return d->attr.color_select;
        default:
            vlog(LogVGA, "3c1 unhandled read from attribute register %02x", d->attr.reg_index);
            return 0;
        }

    case 0x3C4:
        return d->sequencer.reg_index;

    case 0x3C5:
        if (d->sequencer.reg_index > 4) {
            vlog(LogVGA, "Invalid VGA sequencer register #%u read", d->sequencer.reg_index);
            return 0;
        }
        //vlog(LogVGA, "Reading sequencer register %u, data is %02X", d->sequencer.reg_index, d->sequencer.reg[d->sequencer.reg_index]);
        return d->sequencer.reg[d->sequencer.reg_index];

    case 0x3C9: {
        BYTE data = 0;
        RGBColor& color = d->dac.color[d->dac.data_read_index];
        switch (d->dac.data_read_subindex) {
        case 0:
            data = color.red;
            d->dac.data_read_subindex = 1;
            break;
        case 1:
            data = color.green;
            d->dac.data_read_subindex = 2;
            break;
        case 2:
            data = color.blue;
            d->dac.data_read_subindex = 0;
            d->dac.data_read_index += 1;
            break;
        }

        // vlog(LogVGA, "Reading component %u of color %02X (%02X)", dac_data_read_subindex, dac_data_read_index, data);
        return data;
    }

    case 0x3CA:
        vlog(LogVGA, "Reading FCR");
        d->attr.next_3c0_is_index = true;
        return 0x00;

    case 0x3CC:
        return
            (d->misc_output.input_output_address_select << 0) |
            (d->misc_output.ram_enable << 1) |
            (d->misc_output.clock_select << 2) |
            (d->misc_output.odd_even_page_select << 5) |
            (d->misc_output.horizontal_sync_polarity << 6) |
            (d->misc_output.vertical_sync_polarity << 7);

    case 0x3ce:
        return d->graphics_ctrl.reg_index;

    case 0x3cf:
        if (d->graphics_ctrl.reg_index > 8) {
            vlog(LogVGA, "Read from invalid graphics register %u", d->graphics_ctrl.reg_index);
            return 0;
        }
        return d->graphics_ctrl.reg[d->graphics_ctrl.reg_index];

    default:
        vlog(LogVGA, "Unhandled VGA read from %04x", port);
        ASSERT_NOT_REACHED();
        return IODevice::in8(port);
    }
}

WORD VGA::cursor_location() const
{
    return (d->crtc.reg[0x0e] << 8) | d->crtc.reg[0x0f];
}

BYTE VGA::cursor_start_scanline() const
{
    return d->crtc.reg[0x0a] & 0x1f;
}

BYTE VGA::cursor_end_scanline() const
{
    return d->crtc.reg[0x0b] & 0x1f;
}

bool VGA::cursor_enabled() const
{
    return (d->crtc.reg[0x0a] & 0x20) == 0;
}

BYTE VGA::readRegister(BYTE index) const
{
    ASSERT(index <= 0x18);
    return d->crtc.reg[index];
}

void VGA::setPaletteDirty(bool dirty)
{
    if (dirty == d->paletteDirty)
        return;
    d->paletteDirty = dirty;
    emit paletteChanged();
}

bool VGA::isPaletteDirty()
{
    return d->paletteDirty;
}

QColor VGA::paletteColor(int attribute_register_index) const
{
    const RGBColor& c = d->dac.color[d->attr.palette_reg[attribute_register_index]];
    return c;
}

QColor VGA::color(int index) const
{
    const RGBColor& c = d->dac.color[index];
    return c;
}

WORD VGA::start_address() const
{
    return weld<WORD>(d->crtc.reg[0x0C], d->crtc.reg[0x0D]);
}

BYTE VGA::currentVideoMode() const
{
    // FIXME: This is not the correct way to obtain the video mode (BDA.)
    //        Need to find out how the 6845 stores this information.
    return machine().cpu().readPhysicalMemory<BYTE>(PhysicalAddress(0x449)) & 0x7f;
}

bool VGA::inChain4Mode() const
{
    return d->sequencer.reg[0x4] & 0x8;
}

BYTE VGA::write_mode() const
{
    return d->graphics_ctrl.reg[5] & 3;
}

BYTE VGA::read_mode() const
{
    return (d->graphics_ctrl.reg[5] >> 3) & 1;
}

BYTE VGA::rotate_count() const
{
    return d->graphics_ctrl.reg[3] & 7;
}

BYTE VGA::logical_op() const
{
    return (d->graphics_ctrl.reg[3] >> 3) & 3;
}

BYTE VGA::bit_mask() const
{
    return d->graphics_ctrl.reg[8];
}

BYTE VGA::read_map_select() const
{
    return d->graphics_ctrl.reg[4] & 3;
}

void VGA::writeMemory8(DWORD address, BYTE value)
{
    DWORD offset;
    switch (d->graphics_ctrl.memory_map_select) {
    case 0: // A0000h-BFFFFh (128K region)
        if (address < 0xa0000 || address > 0xbffff)
            return;
        offset = address - 0xa0000;
        break;
    case 1: // A0000h-AFFFFh (64K region)
        if (address < 0xa0000 || address > 0xaffff)
            return;
        offset = address - 0xa0000;
        break;
    case 2: // B0000h-B7FFFh (32K region)
        if (address < 0xb0000 || address > 0xb7fff)
            return;
        offset = address - 0xb0000;
        break;
    default: // B8000h-BFFFFh (32K region)
        if (address < 0xb8000 || address > 0xbffff)
            return;
        offset = address - 0xb8000;
        break;
    }

    machine().notifyScreen();

    if (inChain4Mode()) {
        d->memory[(offset & ~0x03) + (offset % 4)*65536] = value;
        return;
    }

    BYTE new_val[4];

    if (write_mode() == 2) {
        new_val[0] = d->latch[0] & ~bit_mask();
        new_val[1] = d->latch[1] & ~bit_mask();
        new_val[2] = d->latch[2] & ~bit_mask();
        new_val[3] = d->latch[3] & ~bit_mask();

        switch (logical_op()) {
        case 0:
            new_val[0] |= (value & 1) ? bit_mask() : 0;
            new_val[1] |= (value & 2) ? bit_mask() : 0;
            new_val[2] |= (value & 4) ? bit_mask() : 0;
            new_val[3] |= (value & 8) ? bit_mask() : 0;
            break;
        default:
            vlog(LogVGA, "Gaah, unsupported draw_op %d in mode 2 :(\n", logical_op());
            hard_exit(1);
        }
    } else if (write_mode() == 0) {
        BYTE set_reset = d->graphics_ctrl.reg[0];
        BYTE enable_set_reset = d->graphics_ctrl.reg[1];
        BYTE val = value;

        if (rotate_count()) {
            vlog(LogVGA, "rotate_count non-zero!");
            val = (val >> rotate_count()) | (val << (8 - rotate_count()));
        }

        new_val[0] = d->latch[0] & ~bit_mask();
        new_val[1] = d->latch[1] & ~bit_mask();
        new_val[2] = d->latch[2] & ~bit_mask();
        new_val[3] = d->latch[3] & ~bit_mask();

        switch (logical_op()) {
        case 0:
            new_val[0] |= ((enable_set_reset & 1)
                ? ((set_reset & 1) ? bit_mask() : 0)
                : (val & bit_mask()));
            new_val[1] |= ((enable_set_reset & 2)
                ? ((set_reset & 2) ? bit_mask() : 0)
                : (val & bit_mask()));
            new_val[2] |= ((enable_set_reset & 4)
                ? ((set_reset & 4) ? bit_mask() : 0)
                : (val & bit_mask()));
            new_val[3] |= ((enable_set_reset & 8)
                ? ((set_reset & 8) ? bit_mask() : 0)
                : (val & bit_mask()));
            break;
        case 1:
            new_val[0] |= ((enable_set_reset & 1)
                ? ((set_reset & 1)
                    ? (~d->latch[0] & bit_mask())
                    : (d->latch[0] & bit_mask()))
                : (val & d->latch[0]) & bit_mask());

            new_val[1] |= ((enable_set_reset & 2)
                ? ((set_reset & 2)
                    ? (~d->latch[1] & bit_mask())
                    : (d->latch[1] & bit_mask()))
                : (val & d->latch[1]) & bit_mask());

            new_val[2] |= ((enable_set_reset & 4)
                ? ((set_reset & 4)
                    ? (~d->latch[2] & bit_mask())
                    : (d->latch[2] & bit_mask()))
                : (val & d->latch[2]) & bit_mask());

            new_val[3] |= ((enable_set_reset & 8)
                ? ((set_reset & 8)
                    ? (~d->latch[3] & bit_mask())
                    : (d->latch[3] & bit_mask()))
                : (val & d->latch[3]) & bit_mask());
            break;
        case 3:
            new_val[0] |= ((enable_set_reset & 1)
                ? ((set_reset & 1)
                    ? (~d->latch[0] & bit_mask())
                    : (d->latch[0] & bit_mask()))
                : (val ^ d->latch[0]) & bit_mask());

            new_val[1] |= ((enable_set_reset & 2)
                ? ((set_reset & 2)
                    ? (~d->latch[1] & bit_mask())
                    : (d->latch[1] & bit_mask()))
                : (val ^ d->latch[1]) & bit_mask());

            new_val[2] |= ((enable_set_reset & 4)
                ? ((set_reset & 4)
                    ? (~d->latch[2] & bit_mask())
                    : (d->latch[2] & bit_mask()))
                : (val ^ d->latch[2]) & bit_mask());

            new_val[3] |= ((enable_set_reset & 8)
                ? ((set_reset & 8)
                    ? (~d->latch[3] & bit_mask())
                    : (d->latch[3] & bit_mask()))
                : (val ^ d->latch[3]) & bit_mask());
            break;
        default:
            vlog(LogVGA, "Unsupported draw_op %u", logical_op());
            hard_exit(0);
        }
    } else if(write_mode() == 1) {
        new_val[0] = d->latch[0];
        new_val[1] = d->latch[1];
        new_val[2] = d->latch[2];
        new_val[3] = d->latch[3];
    } else {
        vlog(LogVGA, "Unsupported 6845 write mode %d", write_mode());
        hard_exit(1);

        /* This is just here to make GCC stop worrying about accessing new_val[] uninitialized. */
        return;
    }

    BYTE map_mask = d->sequencer.reg[2] & 0x0f;

    if (map_mask & 0x01)
        d->plane[0][offset] = new_val[0];
    if (map_mask & 0x02)
        d->plane[1][offset] = new_val[1];
    if (map_mask & 0x04)
        d->plane[2][offset] = new_val[2];
    if (map_mask & 0x08)
        d->plane[3][offset] = new_val[3];
}

BYTE VGA::readMemory8(DWORD address)
{
    DWORD offset;
    switch (d->graphics_ctrl.memory_map_select) {
    case 0: // A0000h-BFFFFh (128K region)
        offset = address & 0x1ffff;
        break;
    case 1: // A0000h-AFFFFh (64K region)
        if (address > 0xaffff)
            return 0xff;
        offset = address & 0xffff;
        break;
    case 2: // B0000h-B7FFFh (32K region)
        if (address < 0xb0000 || address > 0xb7fff)
            return 0xff;
        offset = address & 0x7fff;
        break;
    default: // B8000h-BFFFFh (32K region)
        if (address < 0xb8000 || address > 0xbffff)
            return 0xff;
        offset = address & 0x7fff;
        break;
    }

    if (inChain4Mode())
        return d->memory[(offset & ~3) + (offset % 4) * 65536];

    if (read_mode() != 0) {
        vlog(LogVGA, "ZOMG! READ_MODE = %u", read_mode());
        hard_exit(1);
    }

    d->latch[0] = d->plane[0][offset];
    d->latch[1] = d->plane[1][offset];
    d->latch[2] = d->plane[2][offset];
    d->latch[3] = d->plane[3][offset];

    return d->latch[read_map_select()];
}

const BYTE* VGA::plane(int index) const
{
    ASSERT(index >= 0 && index <= 3);
    return d->plane[index];
}

void VGA::synchronizeColors()
{
    for (int i = 0; i < 16; ++i) {
        d->color[i] = paletteColor(i);
        d->brush[i] = QBrush(d->color[i]);
    }
}
