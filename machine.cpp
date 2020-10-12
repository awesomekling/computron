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

#include "machine.h"
#include "CPU.h"
#include "DiskDrive.h"
#include "PS2.h"
#include "busmouse.h"
#include "cmos.h"
#include "fdc.h"
#include "ide.h"
#include "iodevice.h"
#include "keyboard.h"
#include "machinewidget.h"
#include "pic.h"
#include "pit.h"
#include "screen.h"
#include "settings.h"
#include "vga.h"
#include "vomctl.h"
#include "worker.h"
#include <QtCore/QFile>

OwnPtr<Machine> Machine::create_from_file(const QString& fileName)
{
    auto settings = Settings::create_from_file(fileName);
    if (!settings)
        return nullptr;
    return make<Machine>(std::move(settings));
}

OwnPtr<Machine> Machine::create_for_autotest(const QString& fileName)
{
    auto settings = Settings::create_for_autotest(fileName);
    if (!settings)
        return nullptr;
    return make<Machine>(std::move(settings));
}

Machine::Machine(OwnPtr<Settings>&& settings, QObject* parent)
    : QObject(parent)
    , m_settings(std::move(settings))
{
    m_worker_mutex.lock();
    m_worker = make<Worker>(*this);
    QObject::connect(&worker(), SIGNAL(finished()), this, SLOT(on_worker_finished()));

    worker().start();

    m_worker_waiter.wait(&m_worker_mutex);
    m_worker_mutex.unlock();

    if (!m_settings->is_for_autotest()) {
        // FIXME: Move this somewhere else.
        // Mitigate spam about uninteresting ports.
        IODevice::ignore_port(0x220);
        IODevice::ignore_port(0x221);
        IODevice::ignore_port(0x222);
        IODevice::ignore_port(0x223);
        IODevice::ignore_port(0x201); // Gameport.
        IODevice::ignore_port(0x80);  // Linux outb_p() uses this for small delays.
        IODevice::ignore_port(0x330); // MIDI
        IODevice::ignore_port(0x331); // MIDI
        IODevice::ignore_port(0x334); // SCSI (BusLogic)

        IODevice::ignore_port(0x237);
        IODevice::ignore_port(0x337);

        IODevice::ignore_port(0x322);

        IODevice::ignore_port(0x0C8F);
        IODevice::ignore_port(0x1C8F);
        IODevice::ignore_port(0x2C8F);
        IODevice::ignore_port(0x3C8F);
        IODevice::ignore_port(0x4C8F);
        IODevice::ignore_port(0x5C8F);
        IODevice::ignore_port(0x6C8F);
        IODevice::ignore_port(0x7C8F);
        IODevice::ignore_port(0x8C8F);
        IODevice::ignore_port(0x9C8F);
        IODevice::ignore_port(0xAC8F);
        IODevice::ignore_port(0xBC8F);
        IODevice::ignore_port(0xCC8F);
        IODevice::ignore_port(0xDC8F);
        IODevice::ignore_port(0xEC8F);
        IODevice::ignore_port(0xFC8F);

        IODevice::ignore_port(0x3f6);
    }
}

Machine::~Machine()
{
    qDeleteAll(m_roms);
}

void Machine::did_initialize_worker(Badge<Worker>)
{
    m_worker_waiter.wakeAll();
}

void Machine::make_cpu(Badge<Worker>)
{
    RELEASE_ASSERT(QThread::currentThread() == m_worker.ptr());
    m_cpu = make<CPU>(*this);
}

void Machine::make_devices(Badge<Worker>)
{
    RELEASE_ASSERT(QThread::currentThread() == m_worker.ptr());
    m_floppy0 = make<DiskDrive>("floppy0");
    m_floppy1 = make<DiskDrive>("floppy1");
    m_fixed0 = make<DiskDrive>("fixed0");
    m_fixed1 = make<DiskDrive>("fixed1");

    apply_settings();

    memset(m_fast_input_devices, 0, sizeof(m_fast_input_devices));
    memset(m_fast_output_devices, 0, sizeof(m_fast_output_devices));

    cpu().set_base_memory_size(640 * 1024);

    m_master_pic = make<PIC>(true, *this);
    m_slave_pic = make<PIC>(false, *this);
    m_busmouse = make<BusMouse>(*this);
    m_cmos = make<CMOS>(*this);
    m_fdc = make<FDC>(*this);
    m_ide = make<IDE>(*this);
    m_keyboard = make<Keyboard>(*this);
    m_ps2 = make<PS2>(*this);
    m_vomctl = make<VomCtl>(*this);
    m_pit = make<PIT>(*this);
    m_vga = make<VGA>(*this);

    pit().boot();
}

void Machine::apply_settings()
{
    cpu().set_extended_memory_size(settings().memory_size());
    cpu().set_memory_size_and_reallocate_if_needed(settings().memory_size());

    cpu().set_cs(settings().entry_cs());
    cpu().set_ip(settings().entry_ip());
    cpu().set_ds(settings().entry_ds());
    cpu().set_ss(settings().entry_ss());
    cpu().set_sp(settings().entry_sp());

    QHash<u32, QString> files = settings().files();

    QHash<u32, QString>::const_iterator it = files.constBegin();
    QHash<u32, QString>::const_iterator end = files.constEnd();

    for (; it != end; ++it) {
        if (!load_file(it.key(), it.value())) {
            // FIXME: Should we abort if a load fails?
        }
    }

    for (auto it = settings().rom_images().constBegin(); it != settings().rom_images().constEnd(); ++it) {
        load_rom_image(it.key(), it.value());
    }

    m_floppy0->set_configuration(settings().floppy0());
    m_floppy1->set_configuration(settings().floppy1());
    m_fixed0->set_configuration(settings().fixed0());
    m_fixed1->set_configuration(settings().fixed1());
}

bool Machine::load_file(u32 address, const QString& fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        vlog(LogConfig, "Failed to open %s", qPrintable(fileName));
        return false;
    }

    QByteArray fileContents = file.readAll();

    vlog(LogConfig, "Loading %s at 0x%08X", qPrintable(fileName), address);

    LinearAddress base(address);
    for (int i = 0; i < fileContents.size(); ++i) {
        cpu().write_memory8(base.offset(i), fileContents[i]);
    }
    return true;
}

bool Machine::load_rom_image(u32 address, const QString& fileName)
{
    auto rom = make<ROM>(PhysicalAddress(address), fileName);
    if (!rom->isValid()) {
        vlog(LogConfig, "Failed to load ROM image %s", qPrintable(fileName));
        return false;
    }
    cpu().register_memory_provider(*rom);
    m_roms.append(rom.leakPtr());
    return true;
}

void Machine::start()
{
    worker().exitDebugger();
}

void Machine::pause()
{
    worker().enterDebugger();
}

void Machine::stop()
{
    worker().shutdown();
}

void Machine::reboot()
{
    worker().rebootMachine();
}

void Machine::on_worker_finished()
{
    // FIXME: Implement.
}

bool Machine::is_for_autotest()
{
    return settings().is_for_autotest();
}

void Machine::notify_screen()
{
    if (widget())
        widget()->screen().notify();
}

void Machine::for_each_io_device(std::function<void(IODevice&)> function)
{
    for (IODevice* device : m_allDevices) {
        function(*device);
    }
}

void Machine::reset_all_io_devices()
{
    for_each_io_device([](IODevice& device) {
        device.reset();
    });
}

IODevice* Machine::input_device_for_port_slow_case(u16 port)
{
    return m_all_input_devices.value(port, nullptr);
}

IODevice* Machine::output_device_for_port_slow_case(u16 port)
{
    return m_all_output_devices.value(port, nullptr);
}

void Machine::register_input_device(Badge<IODevice>, u16 port, IODevice& device)
{
    if (port < 1024)
        m_fast_input_devices[port] = &device;
    m_all_input_devices.insert(port, &device);
}

void Machine::register_output_device(Badge<IODevice>, u16 port, IODevice& device)
{
    if (port < 1024)
        m_fast_output_devices[port] = &device;
    m_all_output_devices.insert(port, &device);
}

void Machine::register_device(Badge<IODevice>, IODevice& device)
{
    m_allDevices.insert(&device);
}

void Machine::unregister_device(Badge<IODevice>, IODevice& device)
{
    m_allDevices.remove(&device);
}

DiskDrive& Machine::floppy0()
{
    return *m_floppy0;
}

DiskDrive& Machine::floppy1()
{
    return *m_floppy1;
}

DiskDrive& Machine::fixed0()
{
    return *m_fixed0;
}

DiskDrive& Machine::fixed1()
{
    return *m_fixed1;
}
