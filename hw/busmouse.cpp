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

#include "busmouse.h"

#include "pic.h"
#include "Common.h"
#include "CPU.h"
#include "debug.h"
#include <QtCore/QMutexLocker>

BusMouse::BusMouse(Machine& machine)
    : IODevice("BusMouse", machine, 5)
{
    listen(0x23c, IODevice::ReadWrite);
    listen(0x23d, IODevice::ReadOnly);
    listen(0x23e, IODevice::ReadWrite);

    reset();
}

BusMouse::~BusMouse()
{
}

void BusMouse::reset()
{
    m_interrupts = true;
    m_command = 0;
    m_buttons = 0 ;
    m_currentX = 0;
    m_currentY = 0 ;
    m_lastX = 0;
    m_lastY = 0;
    m_deltaX = 0;
    m_deltaY = 0;
}

void BusMouse::out8(WORD port, BYTE data)
{
    switch (port) {
    case 0x23e:
        m_command = data;
        break;

    case 0x23f:
        switch (data) {
        case 0x10:
            vlog(LogMouse, "Bus mouse interrupt disabled");
            m_interrupts = false;
            break;
        case 0x11:
            vlog(LogMouse, "Bus mouse interrupt enabled");
            m_interrupts = true;
            break;
        default:
            vlog(LogMouse, "Write %02X to %03X, don't know what to do", data, port);
        }
        break;

    case 0x23c:
    case 0x23d:
    default:
        vlog(LogMouse, "Write %02X to %03X, don't know what to do", data, port);
        break;
    }
}

void BusMouse::moveEvent(WORD x, WORD y)
{
    {
        QMutexLocker locker(&m_mutex);
        m_currentX = x;
        m_currentY = y;
    }

    m_deltaX = m_currentX - m_lastX;
    m_deltaY = m_currentY - m_lastY;

    //vlog(LogMouse, "BusMouse::moveEvent(): dX = %d, dY = %d", m_deltaX, m_deltaY);

    if (m_interrupts)
        raiseIRQ();
}

void BusMouse::buttonPressEvent(WORD x, WORD y, MouseButton button)
{
    {
        QMutexLocker locker(&m_mutex);
        if (button == MouseButton::Left)
            m_buttons &= ~(1 << 7);
        else
            m_buttons &= ~(1 << 5);

        m_currentX = x;
        m_currentY = y;
    }

    m_lastX = m_currentX;
    m_lastY = m_currentY;
    m_deltaX = 0;
    m_deltaY = 0;

    if (m_interrupts)
        raiseIRQ();
}

void BusMouse::buttonReleaseEvent(WORD x, WORD y, MouseButton button)
{
    {
        QMutexLocker locker(&m_mutex);
        if (button == MouseButton::Left)
            m_buttons |= (1 << 7);
        else
            m_buttons |= (1 << 5);

        m_currentX = x;
        m_currentY = y;
    }

    m_lastX = m_currentX;
    m_lastY = m_currentY;
    m_deltaX = 0;
    m_deltaY = 0;

    if (m_interrupts)
        raiseIRQ();
}

BYTE BusMouse::in8(WORD port)
{
    static BYTE interrupt_val = 0x01;

    BYTE ret = 0;

    QMutexLocker locker(&m_mutex);

    switch (port) {
    case 0x23c:
        switch (m_command) {
        case 0x90:
        case 0x80: // X LSB
            ret = m_deltaX & 0xF;
            break;
        case 0xb0:
        case 0xa0: // X MSB
            ret = (m_deltaX >> 4) & 0xF;
            m_lastX = m_currentX;
            break;
        case 0xd0:
        case 0xc0: // Y LSB
            ret = m_deltaY & 0xF;
            break;
        case 0xf0:
        case 0xe0: // Y MSB
            ret = (m_deltaY >> 4) & 0xF;
            ret |= m_buttons;
            m_lastY = m_currentY;
            break;
        default:
            vlog(LogMouse, "Unknown BusMouse command %02X", m_command);
        }
        break;

    case 0x23d:
        // Signature byte
        // I believe some OS's expect this to alternate between 0xDE and 0xA5...
        ret = 0xa5;
        break;

    case 0x23e:
        // Stolen from NeXTStep-on-QEMU patches
        ret = interrupt_val;
        interrupt_val = (interrupt_val << 1) & 0xff;
        if (interrupt_val == 0)
            interrupt_val = 1;
        break;

    case 0x23f:
    default:
        vlog(LogMouse, "Read from %03X, don't know what to do", port);
        break;
    }

    return ret;
}
