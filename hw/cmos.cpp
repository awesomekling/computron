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

#include "cmos.h"
#include "CPU.h"
#include "DiskDrive.h"
#include "debug.h"
#include "machine.h"
#include <QtCore/QDate>
#include <QtCore/QTime>

//#define CMOS_DEBUG

CMOS::CMOS(Machine& machine)
    : IODevice("CMOS", machine)
{
    m_rtc_timer = make<ThreadedTimer>(*this, 250);
    listen(0x70, IODevice::WriteOnly);
    listen(0x71, IODevice::ReadWrite);
    reset();
}

CMOS::~CMOS()
{
}

void CMOS::reset()
{
    auto& cpu = machine().cpu();

    memset(m_ram, 0, sizeof(m_ram));
    m_register_index = 0;

    // FIXME: This thing needs more work, 0x26 is just an initial value.
    m_ram[StatusRegisterA] = 0x26;

    m_ram[StatusRegisterB] = 0x02;

    m_ram[BaseMemoryInKilobytesLSB] = least_significant<u8>(cpu.base_memory_size() / 1024);
    m_ram[BaseMemoryInKilobytesMSB] = most_significant<u8>(cpu.base_memory_size() / 1024);
    m_ram[ExtendedMemoryInKilobytesLSB] = least_significant<u8>(cpu.extended_memory_size() / 1024 - 1024);
    m_ram[ExtendedMemoryInKilobytesMSB] = most_significant<u8>(cpu.extended_memory_size() / 1024 - 1024);
    m_ram[ExtendedMemoryInKilobytesAltLSB] = least_significant<u8>(cpu.extended_memory_size() / 1024 - 1024);
    m_ram[ExtendedMemoryInKilobytesAltMSB] = most_significant<u8>(cpu.extended_memory_size() / 1024 - 1024);

    // FIXME: This clearly belongs elsewhere.
    m_ram[FloppyDriveTypes] = (machine().floppy0().floppyTypeForCMOS() << 4) | machine().floppy1().floppyTypeForCMOS();

    update_clock();
}

bool CMOS::in_binary_clock_mode() const
{
    return m_ram[StatusRegisterB] & 0x04;
}

bool CMOS::in_24_hour_mode() const
{
    return m_ram[StatusRegisterB] & 0x02;
}

static QDateTime currentDateTimeForCMOS()
{
#ifdef CT_DETERMINISTIC
    return QDateTime(QDate(2018, 2, 9), QTime(1, 2, 3, 4));
#endif
    return QDateTime::currentDateTime();
}

u8 CMOS::to_current_clock_format(u8 value) const
{
    if (in_binary_clock_mode())
        return value;
    return (value / 10 << 4) | (value - (value / 10) * 10);
}

void CMOS::update_clock()
{
    // FIXME: Support 12-hour clock mode for RTCHour!
    ASSERT(in_24_hour_mode());

    m_ram[StatusRegisterA] |= 0x80; // RTC update in progress
    auto now = currentDateTimeForCMOS();
    m_ram[RTCSecond] = to_current_clock_format(now.time().second());
    m_ram[RTCMinute] = to_current_clock_format(now.time().minute());
    m_ram[RTCHour] = to_current_clock_format(now.time().hour());
    m_ram[RTCDayOfWeek] = to_current_clock_format(now.date().dayOfWeek());
    m_ram[RTCDay] = to_current_clock_format(now.date().day());
    m_ram[RTCMonth] = to_current_clock_format(now.date().month());
    m_ram[RTCYear] = to_current_clock_format(now.date().year() % 100);
    m_ram[RTCCentury] = to_current_clock_format(now.date().year() / 100);
    m_ram[RTCCenturyPS2] = to_current_clock_format(now.date().year() / 100);
    m_ram[StatusRegisterA] &= ~0x80; // RTC update finished
}

u8 CMOS::in8(u16)
{
    u8 value = m_ram[m_register_index];
#ifdef CMOS_DEBUG
    vlog(LogCMOS, "Read register %02x (%02x)", m_register_index, value);
#endif
    return value;
}

void CMOS::out8(u16 port, u8 data)
{
    if (port == 0x70) {
        m_register_index = data & 0x7f;
#ifdef CMOS_DEBUG
        vlog(LogCMOS, "Select register %02x", m_register_index);
#endif
        return;
    }

#ifdef CMOS_DEBUG
    vlog(LogCMOS, "Write register %02x <- %02x", m_register_index, data);
#endif
    m_ram[m_register_index] = data;
}

void CMOS::set(RegisterIndex index, u8 data)
{
    ASSERT((size_t)index < sizeof(m_ram));
    m_ram[index] = data;
}

u8 CMOS::get(RegisterIndex index) const
{
    ASSERT((size_t)index < sizeof(m_ram));
    return m_ram[index];
}

void CMOS::threaded_timer_fired(Badge<ThreadedTimer>)
{
    update_clock();
}
