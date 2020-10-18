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

static const double base_frequency = 1193.1816666; // 1.193182 MHz

enum DecrementMode {
    DecrementBinary = 0,
    DecrementBCD = 1
};
enum CounterAccessState {
    ReadLatchedLSB,
    ReadLatchedMSB,
    AccessMSBOnly,
    AccessLSBOnly,
    AccessLSBThenMSB,
    AccessMSBThenLSB
};

struct CounterInfo {
    u16 start_value { 0xffff };
    u16 reload { 0xffff };
    u16 value();
    u8 mode { 0 };
    DecrementMode decrement_mode { DecrementBinary };
    u16 latched_value { 0xffff };
    CounterAccessState access_state { ReadLatchedLSB };
    u8 format { 0 };
    QElapsedTimer qtimer;

    void check(PIT&);
    bool rolled_over { false };
};

struct PIT::Private {
    CounterInfo counter[3];
    int frequency { 0 };
    OwnPtr<ThreadedTimer> threaded_timer;
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
    int ticks = floor(nsec * base_frequency);

    int current_value = start_value - ticks;
    if (current_value >= reload) {
        vlog(LogTimer, "Current value{%d} >= reload{%d}", current_value, reload);
        if (reload == 0)
            current_value = 0;
        else
            current_value %= reload;
        rolled_over = true;
    } else if (current_value < 0) {
        if (reload == 0)
            current_value = 0;
        else
            current_value = current_value % reload + reload;
        rolled_over = true;
    }

#ifdef PIT_DEBUG
    vlog(LogTimer, "nsec elapsed: %g, ticks: %g, value: %u", nsec, ticks, currentValue);
#endif
    return current_value;
}

void CounterInfo::check(PIT& pit)
{
    value();
    if (rolled_over) {
        if (mode == 0)
            pit.raise_irq();
        rolled_over = false;
    }
}

void PIT::reconfigure_timer(u8 index)
{
    auto& counter = d->counter[index];
    counter.qtimer.start();
}

void PIT::boot()
{
    d->threaded_timer = make<ThreadedTimer>(*this, 5);

    // FIXME: This should be done by the BIOS instead.
    reconfigure_timer(0);
    reconfigure_timer(1);
    reconfigure_timer(2);
}

void PIT::threaded_timer_fired(Badge<ThreadedTimer>)
{
#ifndef CT_DETERMINISTIC
    d->counter[0].check(*this);
    d->counter[1].check(*this);
    d->counter[2].check(*this);
#endif
}

u8 PIT::read_counter(u8 index)
{
    auto& counter = d->counter[index];
    u8 data = 0;
    switch (counter.access_state) {
    case ReadLatchedLSB:
        data = least_significant<u8>(counter.latched_value);
        counter.access_state = ReadLatchedMSB;
        break;
    case ReadLatchedMSB:
        data = most_significant<u8>(counter.latched_value);
        counter.access_state = ReadLatchedLSB;
        break;
    case AccessLSBOnly:
        data = least_significant<u8>(counter.latched_value);
        break;
    case AccessMSBOnly:
        data = most_significant<u8>(counter.latched_value);
        break;
    case AccessLSBThenMSB:
        data = least_significant<u8>(counter.value());
        counter.access_state = AccessMSBThenLSB;
        break;
    case AccessMSBThenLSB:
        data = most_significant<u8>(counter.value());
        counter.access_state = AccessLSBThenMSB;
        break;
    }
    return data;
}

void PIT::write_counter(u8 index, u8 data)
{
    auto& counter = d->counter[index];
    switch (counter.access_state) {
    case ReadLatchedLSB:
    case ReadLatchedMSB:
        break;
    case AccessLSBOnly:
        counter.reload = weld<u16>(most_significant<u8>(counter.reload), data);
        reconfigure_timer(index);
        break;
    case AccessMSBOnly:
        counter.reload = weld<u16>(data, least_significant<u8>(counter.reload));
        reconfigure_timer(index);
        break;
    case AccessLSBThenMSB:
        counter.reload = weld<u16>(most_significant<u8>(counter.reload), data);
        counter.access_state = AccessMSBThenLSB;
        break;
    case AccessMSBThenLSB:
        counter.reload = weld<u16>(data, least_significant<u8>(counter.reload));
        counter.access_state = AccessLSBThenMSB;
        reconfigure_timer(index);
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
        data = read_counter(port - 0x40);
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
        write_counter(port - 0x40, data);
        break;
    case 0x43:
        mode_control(0, data);
        break;
    }
}

void PIT::mode_control(int timer_index, u8 data)
{
    ASSERT(timer_index == 0 || timer_index == 1);

    u8 counter_index = (data >> 6);

    if (counter_index > 2) {
        vlog(LogTimer, "Invalid counter index %d specified.", counter_index);
        return;
    }

    ASSERT(counter_index <= 2);
    CounterInfo& counter = d->counter[counter_index];

    counter.decrement_mode = static_cast<DecrementMode>(data & 1);
    counter.mode = (data >> 1) & 7;

    counter.format = (data >> 4) & 3;
    switch (counter.format) {
    case 0:
        counter.access_state = ReadLatchedLSB;
        counter.latched_value = counter.value();
        break;
    case 1:
        counter.access_state = AccessMSBOnly;
        break;
    case 2:
        counter.access_state = AccessLSBOnly;
        break;
    case 3:
        counter.access_state = AccessLSBThenMSB;
        break;
    }

#ifdef PIT_DEBUG
    vlog(LogTimer, "Setting mode for counter %u { dec: %s, mode: %u, fmt: %02x }",
        counter_index,
        (counter.decrementMode == DecrementBCD) ? "BCD" : "binary",
        counter.mode,
        counter.format);
#endif

    reconfigure_timer(counter_index);
}
