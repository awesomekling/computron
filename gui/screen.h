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

#include "OwnPtr.h"
#include "types.h"
#include <QtCore/QHash>
#include <QtWidgets/QWidget>
#include <QOpenGLWidget>

class Machine;
class Renderer;

class Screen final : public QOpenGLWidget
{
    Q_OBJECT
public:
    explicit Screen(Machine&);
    virtual ~Screen();

    void notify();

    Machine& machine() const { return m_machine; }

    // FIXME: These should be moved into VGA.
    BYTE currentVideoMode() const;
    BYTE currentRowCount() const;
    BYTE currentColumnCount() const;

    void synchronizeColors();

    WORD nextKey();
    WORD peekKey();
    BYTE popKeyData();
    bool hasRawKey();

    void setScreenSize( int width, int height );

protected:
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

public slots:
    void refresh();
    bool loadKeymap(const QString& filename);

private slots:
    void flushKeyBuffer();
    void scheduleRefresh();

private:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void init();

    Renderer& renderer();

    int m_width { 0 };
    int m_height { 0 };

    QImage m_render12;
    QImage m_render04;
    QImage m_render0D;
    QImage m_render13;

    void renderMode13( QImage &target );
    void renderMode12( QImage &target );
    void renderMode0D( QImage &target );
    void renderMode04(QImage &target);

    WORD scanCodeFromKeyEvent(const QKeyEvent*) const;
    QString keyNameFromKeyEvent(const QKeyEvent*) const;

    WORD keyToScanCode(const QString& keyName, Qt::KeyboardModifiers) const;

    QHash<BYTE, QString> m_keyMappings;

    struct Private;
    OwnPtr<Private> d;

    BYTE m_videoModeInLastRefresh { 0xFF };
    Machine& m_machine;
};
