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
#include "ROM.h"
#include "types.h"
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QWaitCondition>
#include <functional>

class IODevice;
class BusMouse;
class CMOS;
class DiskDrive;
class FDC;
class IDE;
class Keyboard;
class PIC;
class PIT;
class PS2;
class Settings;
class CPU;
class VGA;
class VomCtl;
class Worker;
class MachineWidget;

class Machine : public QObject {
    Q_OBJECT
public:
    static OwnPtr<Machine> create_from_file(const QString& fileName);
    static OwnPtr<Machine> create_for_autotest(const QString& fileName);

    explicit Machine(OwnPtr<Settings>&&, QObject* parent = nullptr);
    virtual ~Machine();

    CPU& cpu() { return *m_cpu; }
    VGA& vga() { return *m_vga; }
    PIT& pit() { return *m_pit; }
    BusMouse& busmouse() { return *m_busmouse; }
    Keyboard& keyboard() { return *m_keyboard; }
    VomCtl& vomctl() { return *m_vomctl; }
    PIC& master_pic() { return *m_master_pic; }
    PIC& slave_pic() { return *m_slave_pic; }
    CMOS& cmos() { return *m_cmos; }
    Settings& settings() { return *m_settings; }

    DiskDrive& floppy0();
    DiskDrive& floppy1();
    DiskDrive& fixed0();
    DiskDrive& fixed1();

    bool is_for_autotest() PURE;

    MachineWidget* widget() { return m_widget; }
    void set_widget(MachineWidget* widget) { m_widget = widget; }

    void reset_all_io_devices();
    void notify_screen();

    void for_each_io_device(std::function<void(IODevice&)>);

    IODevice* input_device_for_port(u16 port);
    IODevice* output_device_for_port(u16 port);

    void register_input_device(Badge<IODevice>, u16 port, IODevice&);
    void register_output_device(Badge<IODevice>, u16 port, IODevice&);
    void register_device(Badge<IODevice>, IODevice&);
    void unregister_device(Badge<IODevice>, IODevice&);

    void make_cpu(Badge<Worker>);
    void make_devices(Badge<Worker>);
    void did_initialize_worker(Badge<Worker>);

public slots:
    void start();
    void stop();
    void pause();
    void reboot();

private slots:
    void on_worker_finished();

private:
    bool load_file(u32 address, const QString& fileName);
    bool load_rom_image(u32 address, const QString& fileName);

    void apply_settings();

    Worker& worker() { return *m_worker; }

    IODevice* input_device_for_port_slow_case(u16 port);
    IODevice* output_device_for_port_slow_case(u16 port);

    OwnPtr<Settings> m_settings;
    OwnPtr<CPU> m_cpu;

    OwnPtr<Worker> m_worker;
    QMutex m_worker_mutex;
    QWaitCondition m_worker_waiter;

    // IODevices
    OwnPtr<VGA> m_vga;
    OwnPtr<PIT> m_pit;
    OwnPtr<BusMouse> m_busmouse;
    OwnPtr<CMOS> m_cmos;
    OwnPtr<FDC> m_fdc;
    OwnPtr<IDE> m_ide;
    OwnPtr<Keyboard> m_keyboard;
    OwnPtr<PIC> m_master_pic;
    OwnPtr<PIC> m_slave_pic;
    OwnPtr<PS2> m_ps2;
    OwnPtr<VomCtl> m_vomctl;

    OwnPtr<DiskDrive> m_floppy0;
    OwnPtr<DiskDrive> m_floppy1;
    OwnPtr<DiskDrive> m_fixed0;
    OwnPtr<DiskDrive> m_fixed1;

    MachineWidget* m_widget { nullptr };

    QSet<IODevice*> m_allDevices;

    IODevice* m_fast_input_devices[1024];
    IODevice* m_fast_output_devices[1024];

    QHash<u16, IODevice*> m_all_input_devices;
    QHash<u16, IODevice*> m_all_output_devices;

    QVector<ROM*> m_roms;
};

inline IODevice* Machine::input_device_for_port(u16 port)
{
    if (port < 1024)
        return m_fast_input_devices[port];
    return input_device_for_port_slow_case(port);
}

inline IODevice* Machine::output_device_for_port(u16 port)
{
    if (port < 1024)
        return m_fast_output_devices[port];
    return output_device_for_port_slow_case(port);
}
