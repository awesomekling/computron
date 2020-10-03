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

#include "pit.h"
#include "Common.h"
#include "debug.h"
#include "pic.h"
#include <QElapsedTimer>
#include <math.h>

//#define PIT_DEBUG

static const double baseFrequency = 1193.1816666; // 1.193182 MHz

enum DecrementMode { DecrementBinary = 0,
    DecrementBCD = 1 };
enum CounterAccessState { ReadLatchedLSB,
    ReadLatchedMSB,
    AccessMSBOnly,
    AccessLSBOnly,
    AccessLSBThenMSB,
    AccessMSBThenLSB };

struct CounterInfo {
    u16 startValue { 0xffff };
    u16 reload { 0xffff };
    u16 value();
    u8 mode { 0 };
    DecrementMode decrementMode { DecrementBinary };
    u16 latchedValue { 0xffff };
    CounterAccessState accessState { ReadLatchedLSB };
    u8 format { 0 };
    QElapsedTimer qtimer;

    void check(PIT&);
    bool rolledOver { false };
};

struct PIT::Private {
    CounterInfo counter[3];
    int frequency { 0 };
    OwnPtr<ThreadedTimer> threadedTimer;
};

PIT::PIT(Machine& machine)
    : IODevice("PIT", machine, 0)
    , d(make<Private>())
{
    listen(0x40, IODevice::ReadWrite);
    listen(0x41, IODevice::ReadWrite);
    listen(0x42, IODevice::ReadWrite);
    listen(0x43, IODevice::ReadWrite);

    reset();
}

PIT::~PIT()
{
}

void PIT::reset()
{
    d->frequency = 0;
    d->counter[0] = CounterInfo();
    d->counter[1] = CounterInfo();
    d->counter[2] = CounterInfo();
}

u16 CounterInfo::value()
{
    double nsec = qtimer.nsecsElapsed() / 1000;
    int ticks = floor(nsec * baseFrequency);

    int currentValue = startValue - ticks;
    if (currentValue >= reload) {
        vlog(LogTimer, "Current value{%d} >= reload{%d}", currentValue, reload);
        if (reload == 0)
            currentValue = 0;
        else
            currentValue %= reload;
        rolledOver = true;
    } else if (currentValue < 0) {
        if (reload == 0)
            currentValue = 0;
        else
            currentValue = currentValue % reload + reload;
        rolledOver = true;
    }

#ifdef PIT_DEBUG
    vlog(LogTimer, "nsec elapsed: %g, ticks: %g, value: %u", nsec, ticks, currentValue);
#endif
    return currentValue;
}

void CounterInfo::check(PIT& pit)
{
    value();
    if (rolledOver) {
        if (mode == 0)
            pit.raiseIRQ();
        rolledOver = false;
    }
}

void PIT::reconfigureTimer(u8 index)
{
    auto& counter = d->counter[index];
    counter.qtimer.start();
}

void PIT::boot()
{
    d->threadedTimer = make<ThreadedTimer>(*this, 5);

    // FIXME: This should be done by the BIOS instead.
    reconfigureTimer(0);
    reconfigureTimer(1);
    reconfigureTimer(2);
}

void PIT::threadedTimerFired(Badge<ThreadedTimer>)
{
#ifndef CT_DETERMINISTIC
    d->counter[0].check(*this);
    d->counter[1].check(*this);
    d->counter[2].check(*this);
#endif
}

u8 PIT::readCounter(u8 index)
{
    auto& counter = d->counter[index];
    u8 data = 0;
    switch (counter.accessState) {
    case ReadLatchedLSB:
        data = leastSignificant<u8>(counter.latchedValue);
        counter.accessState = ReadLatchedMSB;
        break;
    case ReadLatchedMSB:
        data = mostSignificant<u8>(counter.latchedValue);
        counter.accessState = ReadLatchedLSB;
        break;
    case AccessLSBOnly:
        data = leastSignificant<u8>(counter.latchedValue);
        break;
    case AccessMSBOnly:
        data = mostSignificant<u8>(counter.latchedValue);
        break;
    case AccessLSBThenMSB:
        data = leastSignificant<u8>(counter.value());
        counter.accessState = AccessMSBThenLSB;
        break;
    case AccessMSBThenLSB:
        data = mostSignificant<u8>(counter.value());
        counter.accessState = AccessLSBThenMSB;
        break;
    }
    return data;
}

void PIT::writeCounter(u8 index, u8 data)
{
    auto& counter = d->counter[index];
    switch (counter.accessState) {
    case ReadLatchedLSB:
    case ReadLatchedMSB:
        break;
    case AccessLSBOnly:
        counter.reload = weld<u16>(mostSignificant<u8>(counter.reload), data);
        reconfigureTimer(index);
        break;
    case AccessMSBOnly:
        counter.reload = weld<u16>(data, leastSignificant<u8>(counter.reload));
        reconfigureTimer(index);
        break;
    case AccessLSBThenMSB:
        counter.reload = weld<u16>(mostSignificant<u8>(counter.reload), data);
        counter.accessState = AccessMSBThenLSB;
        break;
    case AccessMSBThenLSB:
        counter.reload = weld<u16>(data, leastSignificant<u8>(counter.reload));
        counter.accessState = AccessLSBThenMSB;
        reconfigureTimer(index);
        break;
    }
}

u8 PIT::in8(u16 port)
{
    u8 data = 0;
    switch (port) {
    case 0x40:
    case 0x41:
    case 0x42:
        data = readCounter(port - 0x40);
        break;
    case 0x43:
        ASSERT_NOT_REACHED();
        break;
    }

#ifdef PIT_DEBUG
    vlog(LogTimer, " in8 %03x = %02x", port, data);
#endif
    return data;
}

void PIT::out8(u16 port, u8 data)
{
#ifdef PIT_DEBUG
    vlog(LogTimer, "out8 %03x, %02x", port, data);
#endif
    switch (port) {
    case 0x40:
    case 0x41:
    case 0x42:
        writeCounter(port - 0x40, data);
        break;
    case 0x43:
        modeControl(0, data);
        break;
    }
}

void PIT::modeControl(int timerIndex, u8 data)
{
    ASSERT(timerIndex == 0 || timerIndex == 1);

    u8 counterIndex = (data >> 6);

    if (counterIndex > 2) {
        vlog(LogTimer, "Invalid counter index %d specified.", counterIndex);
        return;
    }

    ASSERT(counterIndex <= 2);
    CounterInfo& counter = d->counter[counterIndex];

    counter.decrementMode = static_cast<DecrementMode>(data & 1);
    counter.mode = (data >> 1) & 7;

    counter.format = (data >> 4) & 3;
    switch (counter.format) {
    case 0:
        counter.accessState = ReadLatchedLSB;
        counter.latchedValue = counter.value();
        break;
    case 1:
        counter.accessState = AccessMSBOnly;
        break;
    case 2:
        counter.accessState = AccessLSBOnly;
        break;
    case 3:
        counter.accessState = AccessLSBThenMSB;
        break;
    }

#ifdef PIT_DEBUG
    vlog(LogTimer, "Setting mode for counter %u { dec: %s, mode: %u, fmt: %02x }",
        counterIndex,
        (counter.decrementMode == DecrementBCD) ? "BCD" : "binary",
        counter.mode,
        counter.format);
#endif

    reconfigureTimer(counterIndex);
}
