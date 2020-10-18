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

#include "palettewidget.h"
#include "machine.h"
#include "vga.h"
#include <QtGui/QColor>
#include <QtGui/QPainter>

QSizeF gDabSize(10, 10);

struct PaletteWidget::Private {
    QColor color[256];
};

PaletteWidget::PaletteWidget(Machine& machine, QWidget* parent)
    : QWidget(parent)
    , d(make<Private>())
    , m_machine(machine)
{
    connect(&m_machine.vga(), SIGNAL(palette_changed()), this, SLOT(on_palette_changed()));
}

PaletteWidget::~PaletteWidget()
{
}

void PaletteWidget::on_palette_changed()
{
    for (int i = 0; i < 256; ++i)
        d->color[i] = m_machine.vga().color(i);
    update();
}

QSize PaletteWidget::sizeHint() const
{
    return QSize(gDabSize.width() * 16, gDabSize.height() * 16);
}

void PaletteWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);

    for (int x = 0; x < 16; ++x) {
        for (int y = 0; y < 16; ++y) {
            painter.fillRect(x * gDabSize.width(), y * gDabSize.height(), gDabSize.width(), gDabSize.height(), d->color[y * 16 + x]);
        }
    }
}
