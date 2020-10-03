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

OwnPtr<Machine> Machine::createFromFile(const QString& fileName)
{
    auto settings = Settings::createFromFile(fileName);
    if (!settings)
        return nullptr;
    return make<Machine>(std::move(settings));
}

OwnPtr<Machine> Machine::createForAutotest(const QString& fileName)
{
    auto settings = Settings::createForAutotest(fileName);
    if (!settings)
        return nullptr;
    return make<Machine>(std::move(settings));
}

Machine::Machine(OwnPtr<Settings>&& settings, QObject* parent)
    : QObject(parent)
    , m_settings(std::move(settings))
{
    m_workerMutex.lock();
    m_worker = make<Worker>(*this);
    QObject::connect(&worker(), SIGNAL(finished()), this, SLOT(onWorkerFinished()));

    worker().start();

    m_workerWaiter.wait(&m_workerMutex);
    m_workerMutex.unlock();

    if (!m_settings->isForAutotest()) {
        // FIXME: Move this somewhere else.
        // Mitigate spam about uninteresting ports.
        IODevice::ignorePort(0x220);
        IODevice::ignorePort(0x221);
        IODevice::ignorePort(0x222);
        IODevice::ignorePort(0x223);
        IODevice::ignorePort(0x201); // Gameport.
        IODevice::ignorePort(0x80);  // Linux outb_p() uses this for small delays.
        IODevice::ignorePort(0x330); // MIDI
        IODevice::ignorePort(0x331); // MIDI
        IODevice::ignorePort(0x334); // SCSI (BusLogic)

        IODevice::ignorePort(0x237);
        IODevice::ignorePort(0x337);

        IODevice::ignorePort(0x322);

        IODevice::ignorePort(0x0C8F);
        IODevice::ignorePort(0x1C8F);
        IODevice::ignorePort(0x2C8F);
        IODevice::ignorePort(0x3C8F);
        IODevice::ignorePort(0x4C8F);
        IODevice::ignorePort(0x5C8F);
        IODevice::ignorePort(0x6C8F);
        IODevice::ignorePort(0x7C8F);
        IODevice::ignorePort(0x8C8F);
        IODevice::ignorePort(0x9C8F);
        IODevice::ignorePort(0xAC8F);
        IODevice::ignorePort(0xBC8F);
        IODevice::ignorePort(0xCC8F);
        IODevice::ignorePort(0xDC8F);
        IODevice::ignorePort(0xEC8F);
        IODevice::ignorePort(0xFC8F);

        IODevice::ignorePort(0x3f6);
    }
}

Machine::~Machine()
{
    qDeleteAll(m_roms);
}

void Machine::didInitializeWorker(Badge<Worker>)
{
    m_workerWaiter.wakeAll();
}

void Machine::makeCPU(Badge<Worker>)
{
    RELEASE_ASSERT(QThread::currentThread() == m_worker.ptr());
    m_cpu = make<CPU>(*this);
}

void Machine::makeDevices(Badge<Worker>)
{
    RELEASE_ASSERT(QThread::currentThread() == m_worker.ptr());
    m_floppy0 = make<DiskDrive>("floppy0");
    m_floppy1 = make<DiskDrive>("floppy1");
    m_fixed0 = make<DiskDrive>("fixed0");
    m_fixed1 = make<DiskDrive>("fixed1");

    applySettings();

    memset(m_fastInputDevices, 0, sizeof(m_fastInputDevices));
    memset(m_fastOutputDevices, 0, sizeof(m_fastOutputDevices));

    cpu().setBaseMemorySize(640 * 1024);

    m_masterPIC = make<PIC>(true, *this);
    m_slavePIC = make<PIC>(false, *this);
    m_busMouse = make<BusMouse>(*this);
    m_cmos = make<CMOS>(*this);
    m_fdc = make<FDC>(*this);
    m_ide = make<IDE>(*this);
    m_keyboard = make<Keyboard>(*this);
    m_ps2 = make<PS2>(*this);
    m_vomCtl = make<VomCtl>(*this);
    m_pit = make<PIT>(*this);
    m_vga = make<VGA>(*this);

    pit().boot();
}

void Machine::applySettings()
{
    cpu().setExtendedMemorySize(settings().memorySize());
    cpu().setMemorySizeAndReallocateIfNeeded(settings().memorySize());

    cpu().setCS(settings().entryCS());
    cpu().setIP(settings().entryIP());
    cpu().setDS(settings().entryDS());
    cpu().setSS(settings().entrySS());
    cpu().setSP(settings().entrySP());

    QHash<DWORD, QString> files = settings().files();

    QHash<DWORD, QString>::const_iterator it = files.constBegin();
    QHash<DWORD, QString>::const_iterator end = files.constEnd();

    for (; it != end; ++it) {
        if (!loadFile(it.key(), it.value())) {
            // FIXME: Should we abort if a load fails?
        }
    }

    for (auto it = settings().romImages().constBegin(); it != settings().romImages().constEnd(); ++it) {
        loadROMImage(it.key(), it.value());
    }

    m_floppy0->setConfiguration(settings().floppy0());
    m_floppy1->setConfiguration(settings().floppy1());
    m_fixed0->setConfiguration(settings().fixed0());
    m_fixed1->setConfiguration(settings().fixed1());
}

bool Machine::loadFile(DWORD address, const QString& fileName)
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
        cpu().writeMemory8(base.offset(i), fileContents[i]);
    }
    return true;
}

bool Machine::loadROMImage(DWORD address, const QString& fileName)
{
    auto rom = make<ROM>(PhysicalAddress(address), fileName);
    if (!rom->isValid()) {
        vlog(LogConfig, "Failed to load ROM image %s", qPrintable(fileName));
        return false;
    }
    cpu().registerMemoryProvider(*rom);
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

void Machine::onWorkerFinished()
{
    // FIXME: Implement.
}

bool Machine::isForAutotest()
{
    return settings().isForAutotest();
}

void Machine::notifyScreen()
{
    if (widget())
        widget()->screen().notify();
}

void Machine::forEachIODevice(std::function<void(IODevice&)> function)
{
    for (IODevice* device : m_allDevices) {
        function(*device);
    }
}

void Machine::resetAllIODevices()
{
    forEachIODevice([](IODevice& device) {
        device.reset();
    });
}

IODevice* Machine::inputDeviceForPortSlowCase(WORD port)
{
    return m_allInputDevices.value(port, nullptr);
}

IODevice* Machine::outputDeviceForPortSlowCase(WORD port)
{
    return m_allOutputDevices.value(port, nullptr);
}

void Machine::registerInputDevice(Badge<IODevice>, WORD port, IODevice& device)
{
    if (port < 1024)
        m_fastInputDevices[port] = &device;
    m_allInputDevices.insert(port, &device);
}

void Machine::registerOutputDevice(Badge<IODevice>, WORD port, IODevice& device)
{
    if (port < 1024)
        m_fastOutputDevices[port] = &device;
    m_allOutputDevices.insert(port, &device);
}

void Machine::registerDevice(Badge<IODevice>, IODevice& device)
{
    m_allDevices.insert(&device);
}

void Machine::unregisterDevice(Badge<IODevice>, IODevice& device)
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
