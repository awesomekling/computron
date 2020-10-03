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

#pragma once

#include "types.h"
#include <QBitmap>
#include <QBrush>
#include <QImage>

class Screen;
class VGA;

class Renderer {
public:
    const Screen& screen() const;
    const VGA& vga() const;

    virtual void synchronizeFont() = 0;
    virtual void synchronizeColors() = 0;
    virtual void willBecomeActive() = 0;
    virtual void render() = 0;
    virtual void paint(QPainter&) = 0;

protected:
    explicit Renderer(Screen& screen)
        : m_screen(screen)
    {
    }

private:
    Screen& m_screen;
};

class TextRenderer final : public Renderer {
public:
    explicit TextRenderer(Screen& screen)
        : Renderer(screen)
    {
    }

    virtual void synchronizeFont() override;
    virtual void synchronizeColors() override;
    virtual void willBecomeActive() override;
    virtual void render() override { }
    virtual void paint(QPainter&) override;

private:
    void putCharacter(QPainter&, int row, int column, u8 color, u8 character);

    int m_rows { 25 };
    int m_columns { 80 };
    int m_characterWidth { 8 };
    int m_characterHeight { 16 };

    QBitmap m_character[256];
    QBrush m_brush[16];
    QColor m_color[16];
};

class DummyRenderer final : public Renderer {
public:
    explicit DummyRenderer(Screen& screen)
        : Renderer(screen)
    {
    }

    virtual void synchronizeFont() override { }
    virtual void synchronizeColors() override { }
    virtual void willBecomeActive() override { }
    virtual void render() override { }
    virtual void paint(QPainter&) override { }
};

class BufferedRenderer : public Renderer {
public:
    virtual void paint(QPainter&) override;
    virtual void willBecomeActive() override;

protected:
    explicit BufferedRenderer(Screen&, int width, int height, int scale = 1);
    u8* bufferBits() { return m_buffer.bits(); }

    QImage m_buffer;
    int m_scale { 1 };
};

class Mode04Renderer final : public BufferedRenderer {
public:
    explicit Mode04Renderer(Screen&);

    virtual void synchronizeFont() override { }
    virtual void synchronizeColors() override { }
    virtual void render() override;
};

class Mode0DRenderer final : public BufferedRenderer {
public:
    explicit Mode0DRenderer(Screen& screen)
        : BufferedRenderer(screen, 320, 200, 2)
    {
    }

    virtual void synchronizeFont() override { }
    virtual void synchronizeColors() override;
    virtual void render() override;
};

class Mode12Renderer final : public BufferedRenderer {
public:
    explicit Mode12Renderer(Screen& screen)
        : BufferedRenderer(screen, 640, 480)
    {
    }

    virtual void synchronizeFont() override { }
    virtual void synchronizeColors() override;
    virtual void render() override;
};

class Mode13Renderer final : public BufferedRenderer {
public:
    explicit Mode13Renderer(Screen& screen)
        : BufferedRenderer(screen, 320, 200, 2)
    {
    }

    virtual void synchronizeFont() override { }
    virtual void synchronizeColors() override;
    virtual void render() override;
};
