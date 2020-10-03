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
    static OwnPtr<Machine> createFromFile(const QString& fileName);
    static OwnPtr<Machine> createForAutotest(const QString& fileName);

    explicit Machine(OwnPtr<Settings>&&, QObject* parent = nullptr);
    virtual ~Machine();

    CPU& cpu() { return *m_cpu; }
    VGA& vga() { return *m_vga; }
    PIT& pit() { return *m_pit; }
    BusMouse& busMouse() { return *m_busMouse; }
    Keyboard& keyboard() { return *m_keyboard; }
    VomCtl& vomCtl() { return *m_vomCtl; }
    PIC& masterPIC() { return *m_masterPIC; }
    PIC& slavePIC() { return *m_slavePIC; }
    CMOS& cmos() { return *m_cmos; }
    Settings& settings() { return *m_settings; }

    DiskDrive& floppy0();
    DiskDrive& floppy1();
    DiskDrive& fixed0();
    DiskDrive& fixed1();

    bool isForAutotest() PURE;

    MachineWidget* widget() { return m_widget; }
    void setWidget(MachineWidget* widget) { m_widget = widget; }

    void resetAllIODevices();
    void notifyScreen();

    void forEachIODevice(std::function<void(IODevice&)>);

    IODevice* inputDeviceForPort(u16 port);
    IODevice* outputDeviceForPort(u16 port);

    void registerInputDevice(Badge<IODevice>, u16 port, IODevice&);
    void registerOutputDevice(Badge<IODevice>, u16 port, IODevice&);
    void registerDevice(Badge<IODevice>, IODevice&);
    void unregisterDevice(Badge<IODevice>, IODevice&);

    void makeCPU(Badge<Worker>);
    void makeDevices(Badge<Worker>);
    void didInitializeWorker(Badge<Worker>);

public slots:
    void start();
    void stop();
    void pause();
    void reboot();

private slots:
    void onWorkerFinished();

private:
    bool loadFile(u32 address, const QString& fileName);
    bool loadROMImage(u32 address, const QString& fileName);

    void applySettings();

    Worker& worker() { return *m_worker; }

    IODevice* inputDeviceForPortSlowCase(u16 port);
    IODevice* outputDeviceForPortSlowCase(u16 port);

    OwnPtr<Settings> m_settings;
    OwnPtr<CPU> m_cpu;

    OwnPtr<Worker> m_worker;
    QMutex m_workerMutex;
    QWaitCondition m_workerWaiter;

    // IODevices
    OwnPtr<VGA> m_vga;
    OwnPtr<PIT> m_pit;
    OwnPtr<BusMouse> m_busMouse;
    OwnPtr<CMOS> m_cmos;
    OwnPtr<FDC> m_fdc;
    OwnPtr<IDE> m_ide;
    OwnPtr<Keyboard> m_keyboard;
    OwnPtr<PIC> m_masterPIC;
    OwnPtr<PIC> m_slavePIC;
    OwnPtr<PS2> m_ps2;
    OwnPtr<VomCtl> m_vomCtl;

    OwnPtr<DiskDrive> m_floppy0;
    OwnPtr<DiskDrive> m_floppy1;
    OwnPtr<DiskDrive> m_fixed0;
    OwnPtr<DiskDrive> m_fixed1;

    MachineWidget* m_widget { nullptr };

    QSet<IODevice*> m_allDevices;

    IODevice* m_fastInputDevices[1024];
    IODevice* m_fastOutputDevices[1024];

    QHash<u16, IODevice*> m_allInputDevices;
    QHash<u16, IODevice*> m_allOutputDevices;

    QVector<ROM*> m_roms;
};

inline IODevice* Machine::inputDeviceForPort(u16 port)
{
    if (port < 1024)
        return m_fastInputDevices[port];
    return inputDeviceForPortSlowCase(port);
}

inline IODevice* Machine::outputDeviceForPort(u16 port)
{
    if (port < 1024)
        return m_fastOutputDevices[port];
    return outputDeviceForPortSlowCase(port);
}
