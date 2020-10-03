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

#include "Renderer.h"
#include "CPU.h"
#include "Common.h"
#include "machine.h"
#include "screen.h"
#include "vga.h"
#include <QPainter>

struct fontcharbitmap_t {
    u8 data[16];
};

const Screen& Renderer::screen() const
{
    return m_screen;
}

const VGA& Renderer::vga() const
{
    return m_screen.machine().vga();
}

BufferedRenderer::BufferedRenderer(Screen& screen, int width, int height, int scale)
    : Renderer(screen)
    , m_buffer(width, height, QImage::Format_Indexed8)
    , m_scale(scale)
{
    m_buffer.fill(0);
}

Mode04Renderer::Mode04Renderer(Screen& screen)
    : BufferedRenderer(screen, 320, 200, 2)
{
    m_buffer.setColor(0, QColor(Qt::black).rgb());
    m_buffer.setColor(1, QColor(Qt::cyan).rgb());
    m_buffer.setColor(2, QColor(Qt::magenta).rgb());
    m_buffer.setColor(3, QColor(Qt::white).rgb());
}

void TextRenderer::putCharacter(QPainter& p, int row, int column, u8 color, u8 character)
{
    int x = column * m_characterWidth;
    int y = row * m_characterHeight;

    p.setBackground(m_brush[color >> 4]);
    p.eraseRect(x, y, m_characterWidth, m_characterHeight);
    p.setPen(m_color[color & 0xf]);
    p.drawPixmap(x, y, m_character[character]);
}

void Mode04Renderer::render()
{
    const u8* video_memory = vga().text_memory() + vga().start_address();
    for (unsigned scanLine = 0; scanLine < 200; ++scanLine) {
        u8* out = m_buffer.scanLine(scanLine);
        const u8* in = video_memory;
        if ((scanLine & 1))
            in += 0x2000;
        in += (scanLine / 2) * 80;
        for (unsigned i = 0; i < 80; ++i) {
            *(out++) = (in[i] >> 6) & 3;
            *(out++) = (in[i] >> 4) & 3;
            *(out++) = (in[i] >> 2) & 3;
            *(out++) = (in[i] >> 0) & 3;
        }
    }
}

void Mode12Renderer::render()
{
    const u8* p0 = vga().plane(0);
    const u8* p1 = vga().plane(1);
    const u8* p2 = vga().plane(2);
    const u8* p3 = vga().plane(3);

    int offset = 0;

    u8* bits = bufferBits();
    for (int y = 0; y < 480; ++y) {
        u8* px = &bits[y * 640];

        for (int x = 0; x < 640; x += 8, ++offset) {
#define D(i) ((p0[offset] >> i) & 1) | (((p1[offset] >> i) & 1) << 1) | (((p2[offset] >> i) & 1) << 2) | (((p3[offset] >> i) & 1) << 3)
            *(px++) = D(7);
            *(px++) = D(6);
            *(px++) = D(5);
            *(px++) = D(4);
            *(px++) = D(3);
            *(px++) = D(2);
            *(px++) = D(1);
            *(px++) = D(0);
        }
    }
}

void Mode0DRenderer::render()
{
    const u8* p0 = vga().plane(0);
    const u8* p1 = vga().plane(1);
    const u8* p2 = vga().plane(2);
    const u8* p3 = vga().plane(3);

    u16 start_address = vga().start_address();
    p0 += start_address;
    p1 += start_address;
    p2 += start_address;
    p3 += start_address;

    u8* bits = bufferBits();
    int offset = 0;

    for (int y = 0; y < 200; ++y) {
        u8* px = &bits[y * 320];
#define A0D(i) ((p0[offset] >> i) & 1) | (((p1[offset] >> i) & 1) << 1) | (((p2[offset] >> i) & 1) << 2) | (((p3[offset] >> i) & 1) << 3)
        for (int x = 0; x < 320; x += 8, ++offset) {
            *(px++) = D(7);
            *(px++) = D(6);
            *(px++) = D(5);
            *(px++) = D(4);
            *(px++) = D(3);
            *(px++) = D(2);
            *(px++) = D(1);
            *(px++) = D(0);
        }
    }
}

void BufferedRenderer::willBecomeActive()
{
    const_cast<Screen&>(screen()).setScreenSize(m_buffer.width() * m_scale, m_buffer.height() * m_scale);
}

void BufferedRenderer::paint(QPainter& p)
{
    p.drawImage(QRect(0, 0, m_buffer.width() * m_scale, m_buffer.height() * m_scale), m_buffer);
}

void Mode0DRenderer::synchronizeColors()
{
    for (unsigned i = 0; i < 16; ++i)
        m_buffer.setColor(i, vga().paletteColor(i).rgb());
}

void Mode12Renderer::synchronizeColors()
{
    for (unsigned i = 0; i < 16; ++i)
        m_buffer.setColor(i, vga().paletteColor(i).rgb());
}

void Mode13Renderer::synchronizeColors()
{
    for (unsigned i = 0; i < 256; ++i)
        m_buffer.setColor(i, vga().color(i).rgb());
}

void Mode13Renderer::render()
{
    const u8* videoMemory = vga().plane(0) + vga().start_address();

    ValueSize mode;
    u32 lineOffset = vga().readRegister(0x13);

    if (vga().readRegister(0x14) & 0x40) {
        mode = DWordSize;
        lineOffset <<= 3;
    } else if (vga().readRegister(0x17) & 0x40) {
        mode = ByteSize;
        lineOffset <<= 1;
    } else {
        mode = WordSize;
        lineOffset <<= 2;
    }

    auto* bits = bufferBits();
    auto* bit = bits;

    if (mode == ByteSize) {
        for (unsigned y = 0; y < 200; ++y) {
            for (unsigned x = 0; x < 320; ++x) {
                u8 plane = x % 4;
                u32 byteOffset = (plane * 65536) + (y * lineOffset) + (x >> 2);
                *(bit++) = videoMemory[byteOffset];
            }
        }
    } else if (mode == WordSize) {
        for (unsigned y = 0; y < 200; ++y) {
            for (unsigned x = 0; x < 320; ++x) {
                u8 plane = x % 4;
                u32 byteOffset = (plane * 65536) + (y * lineOffset) + ((x >> 1) & ~1);
                *(bit++) = videoMemory[byteOffset];
            }
        }
    } else if (mode == DWordSize) {
        for (unsigned y = 0; y < 200; ++y) {
            for (unsigned x = 0; x < 320; ++x) {
                u8 plane = x % 4;
                u32 byteOffset = (plane * 65536) + (y * lineOffset) + (x & ~3);
                *(bit++) = videoMemory[byteOffset];
            }
        }
    }
}

void TextRenderer::willBecomeActive()
{
    const_cast<Screen&>(screen()).setScreenSize(m_characterWidth * m_columns, m_characterHeight * m_rows);
}

void TextRenderer::paint(QPainter& p)
{
    auto* text_ptr = vga().text_memory() + vga().start_address() * 2;

    // Repaint everything
    for (int y = 0; y < m_rows; ++y) {
        for (int x = 0; x < m_columns; ++x) {
            putCharacter(p, y, x, text_ptr[1], text_ptr[0]);
            text_ptr += 2;
        }
    }

    if (vga().cursor_enabled()) {
        u16 raw_cursor = vga().cursor_location() - vga().start_address();
        u16 screen_columns = screen().currentColumnCount();
        u16 row = screen_columns ? (raw_cursor / screen_columns) : 0;
        u16 column = screen_columns ? (raw_cursor % screen_columns) : 0;
        u8 cursor_start = vga().cursor_start_scanline();
        u8 cursor_end = vga().cursor_end_scanline();

        p.fillRect(
            column * m_characterWidth,
            row * m_characterHeight + cursor_start,
            m_characterWidth,
            cursor_end - cursor_start,
            m_brush[14]);
    }
}

void TextRenderer::synchronizeColors()
{
    for (int i = 0; i < 16; ++i) {
        m_color[i] = vga().paletteColor(i);
        m_brush[i] = QBrush(m_color[i]);
    }
}

void TextRenderer::synchronizeFont()
{
    auto vector = screen().machine().cpu().getRealModeInterruptVector(0x43);
    auto physicalAddress = PhysicalAddress::fromRealMode(vector);
    auto* fbmp = (const fontcharbitmap_t*)(screen().machine().cpu().pointerToPhysicalMemory(physicalAddress));

    for (int i = 0; i < 256; ++i)
        m_character[i] = QBitmap::fromData(QSize(m_characterWidth, m_characterHeight), fbmp[i].data, QImage::Format_Mono);
}
