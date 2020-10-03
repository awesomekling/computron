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

#include "MouseObserver.h"
#include "iodevice.h"
#include <QtCore/QMutex>

class BusMouse final : public IODevice
    , public MouseObserver {
public:
    explicit BusMouse(Machine&);
    virtual ~BusMouse() override;

    virtual void reset() override;
    virtual void out8(u16 port, u8 data) override;
    virtual u8 in8(u16 port) override;

    virtual void moveEvent(u16 x, u16 y) override;
    virtual void buttonPressEvent(u16 x, u16 y, MouseButton) override;
    virtual void buttonReleaseEvent(u16 x, u16 y, MouseButton) override;

    static BusMouse* the();

private:
    bool m_interrupts { true };
    u8 m_command { 0 };
    u8 m_buttons { 0 };

    u16 m_currentX { 0 };
    u16 m_currentY { 0 };
    u16 m_lastX { 0 };
    u16 m_lastY { 0 };
    u16 m_deltaX { 0 };
    u16 m_deltaY { 0 };

    QMutex m_mutex;
};
