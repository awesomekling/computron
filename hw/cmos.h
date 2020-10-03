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

#include "Common.h"
#include "OwnPtr.h"
#include "ThreadedTimer.h"
#include "iodevice.h"

class CMOS final : public IODevice
    , public ThreadedTimer::Listener {
public:
    enum RegisterIndex {
        StatusRegisterA = 0x0a,
        StatusRegisterB = 0x0b,
        FloppyDriveTypes = 0x10,
        BaseMemoryInKilobytesLSB = 0x15,
        BaseMemoryInKilobytesMSB = 0x16,
        ExtendedMemoryInKilobytesLSB = 0x17,
        ExtendedMemoryInKilobytesMSB = 0x18,
        ExtendedMemoryInKilobytesAltLSB = 0x30,
        ExtendedMemoryInKilobytesAltMSB = 0x31,
        RTCSecond = 0x00,
        RTCMinute = 0x02,
        RTCHour = 0x04,
        RTCDayOfWeek = 0x06,
        RTCDay = 0x07,
        RTCMonth = 0x08,
        RTCYear = 0x09,
        RTCCentury = 0x32,
        RTCCenturyPS2 = 0x37,
    };

    explicit CMOS(Machine&);
    ~CMOS();

    void reset() override;
    void out8(WORD port, BYTE data) override;
    BYTE in8(WORD port) override;

    void updateClock();

    void set(RegisterIndex, BYTE);
    BYTE get(RegisterIndex) const;

private:
    virtual void threadedTimerFired(Badge<ThreadedTimer>) override;

    BYTE m_registerIndex { 0 };
    BYTE m_ram[80];

    bool inBinaryClockMode() const;
    bool in24HourMode() const;
    BYTE toCurrentClockFormat(BYTE) const;

    OwnPtr<ThreadedTimer> m_rtcTimer;
};
