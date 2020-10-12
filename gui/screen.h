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
#include <QOpenGLWidget>
#include <QtCore/QHash>
#include <QtWidgets/QWidget>

class Machine;
class MouseObserver;
class Renderer;

class Screen final : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit Screen(Machine&);
    virtual ~Screen();

    void notify();

    Machine& machine() const { return m_machine; }

    // FIXME: These should be moved into VGA.
    u8 current_video_mode() const;
    u8 current_row_count() const;
    u8 current_column_count() const;

    u16 next_key();
    u16 peek_key();
    u8 pop_key_data();
    bool has_raw_key();

    void set_screen_size(int width, int height);

protected:
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

public slots:
    void refresh();
    bool load_keymap(const QString& filename);

private slots:
    void flush_key_buffer();
    void schedule_refresh();

private:
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void init();

    MouseObserver& mouse_observer();

    Renderer& renderer();

    int m_width { 0 };
    int m_height { 0 };

    u16 scan_code_from_key_event(const QKeyEvent*) const;
    QString key_name_from_key_event(const QKeyEvent*) const;

    u16 key_to_scan_code(const QString& key_name, Qt::KeyboardModifiers) const;

    QHash<u8, QString> m_key_mappings;

    struct Private;
    OwnPtr<Private> d;

    u8 m_video_mode_in_last_refresh { 0xFF };
    Machine& m_machine;
};
