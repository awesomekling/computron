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

#include "iodevice.h"

class Keyboard final : public QObject
    , public IODevice {
    Q_OBJECT
public:
    enum LED {
        ScrollLock = 1,
        NumLock = 2,
        CapsLock = 4,
    };

    explicit Keyboard(Machine&);
    virtual ~Keyboard();

    virtual void reset() override;
    virtual u8 in8(u16 port) override;
    virtual void out8(u16 port, u8 data) override;

    bool is_enabled() const { return m_enabled; }

    void did_enqueue_data();

signals:
    void leds_changed(int);

private:
    u8 m_system_control_port_data;
    u8 m_ram[64];
    u8 m_command;
    bool m_has_command;
    bool m_last_was_command;
    u8 m_leds { 0 };
    bool m_enabled { true };
};
