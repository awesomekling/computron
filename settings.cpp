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

#include "settings.h"
#include "Common.h"
#include "debug.h"
#include <QFile>
#include <QStringList>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct FloppyType {
    const char* name;
    u16 sectorsPerTrack;
    u16 heads;
    u32 sectors;
    u16 bytesPerSector;
    u8 mediaType;
};

static FloppyType gFloppyTypes[] = {
    { "1.44M", 18, 2, 2880, 512, 4 },
    { "720kB", 9, 2, 1440, 512, 3 },
    { "1.2M", 15, 2, 2400, 512, 2 },
    { "360kB", 9, 2, 720, 512, 1 },
    { "320kB", 8, 2, 640, 512, 0 },
    { "160kB", 8, 1, 320, 512, 0 },
    { 0L, 0, 0, 0, 0, 0 }
};

static bool parseAddress(const QString& string, u32* address)
{
    ASSERT(address);

    QStringList parts = string.split(QLatin1Char(':'), QString::SkipEmptyParts);
    if (parts.count() != 2)
        return false;

    bool ok;
    u16 segment = parts.at(0).toUInt(&ok, 16);
    if (!ok)
        return false;

    u32 offset = parts.at(1).toUInt(&ok, 16);
    if (!ok)
        return false;

    *address = realModeAddressToPhysicalAddress(segment, offset).get();
    return true;
}

bool Settings::handleLoadFile(const QStringList& arguments)
{
    // load-file <segment:offset> <path/to/file>

    if (arguments.count() != 2)
        return false;

    u32 address;
    if (!parseAddress(arguments.at(0), &address))
        return false;

    m_files.insert(address, arguments.at(1));
    return true;
}

bool Settings::handleROMImage(const QStringList& arguments)
{
    // load-file <physical-address> <path/to/file>

    if (arguments.count() != 2)
        return false;

    bool ok;
    u32 address = arguments.at(0).toUInt(&ok, 16);
    if (!ok)
        return false;

    m_romImages.insert(address, arguments.at(1));
    return true;
}

bool Settings::handleMemorySize(const QStringList& arguments)
{
    // memory-size <size>

    if (arguments.count() != 1)
        return false;

    bool ok;
    unsigned size = arguments.at(0).toUInt(&ok);
    if (!ok)
        return false;

    setMemorySize(size * 1024);
    return true;
}

bool Settings::handleKeymap(const QStringList& arguments)
{
    // keymap <path/to/file>

    if (arguments.count() != 1)
        return false;

    QString fileName = arguments.at(0);
    if (!QFile::exists(fileName))
        return false;

    vlog(LogConfig, "Keymap %s", qPrintable(fileName));
    m_keymap = fileName;
    return true;
}

bool Settings::handleFixedDisk(const QStringList& arguments)
{
    // fixed-disk <index> <path/to/file> <size>

    if (arguments.count() != 3)
        return false;

    bool ok;
    unsigned index = arguments.at(0).toUInt(&ok);
    if (!ok)
        return false;
    if (index > 1)
        return false;

    QString fileName = arguments.at(1);

    unsigned size = arguments.at(2).toUInt(&ok);
    if (!ok)
        return false;

    vlog(LogConfig, "Fixed disk %u: %s (%ld KiB)", index, qPrintable(fileName), size);

    DiskDrive::Configuration& config = index == 0 ? m_fixed0 : m_fixed1;
    config.imagePath = fileName;
    config.sectorsPerTrack = 63;
    config.heads = 16;
    config.bytesPerSector = 512;
    config.sectors = (size * 1024) / config.bytesPerSector;

    return true;
}

bool Settings::handleFloppyDisk(const QStringList& arguments)
{
    // floppy-disk <index> <type> <path/to/file>

    if (arguments.count() != 3)
        return false;

    bool ok;
    unsigned index = arguments.at(0).toUInt(&ok);
    if (!ok)
        return false;
    if (index > 1)
        return false;

    QString type = arguments.at(1);
    QString fileName = arguments.at(2);

    FloppyType* ft;
    for (ft = gFloppyTypes; ft->name; ++ft) {
        if (type == QLatin1String(ft->name))
            break;
    }

    if (!ft->name) {
        vlog(LogConfig, "Invalid floppy type: \"%s\"", qPrintable(type));
        return false;
    }

    DiskDrive::Configuration& config = index == 0 ? m_floppy0 : m_floppy1;
    config.imagePath = fileName;
    config.sectorsPerTrack = ft->sectorsPerTrack;
    config.heads = ft->heads;
    config.sectors = ft->sectors;
    config.floppyTypeForCMOS = ft->mediaType;
    config.bytesPerSector = ft->bytesPerSector;

    vlog(LogConfig, "Floppy %u: %s (%uspt, %uh, %us (%ub))", index, qPrintable(fileName), config.sectorsPerTrack, config.heads, config.sectors, config.bytesPerSector);
    return true;
}

OwnPtr<Settings> Settings::createForAutotest(const QString& fileName)
{
    static const u16 autotestEntryCS = 0x1000;
    static const u16 autotestEntryIP = 0x0000;
    static const u16 autotestEntryDS = 0x1000;
    static const u16 autotestEntrySS = 0x9000;
    static const u16 autotestEntrySP = 0x1000;

    auto settings = make<Settings>();

    settings->m_entryCS = autotestEntryCS;
    settings->m_entryIP = autotestEntryIP;
    settings->m_entryDS = autotestEntryDS;
    settings->m_entrySS = autotestEntrySS;
    settings->m_entrySP = autotestEntrySP;
    settings->m_files.insert(realModeAddressToPhysicalAddress(autotestEntryCS, autotestEntryIP).get(), fileName);

    settings->m_forAutotest = true;
    return settings;
}

OwnPtr<Settings> Settings::createFromFile(const QString& fileName)
{
    QFile file(fileName);

    if (!file.open(QIODevice::ReadOnly)) {
        vlog(LogConfig, "Couldn't load %s", qPrintable(fileName));
        return nullptr;
    }

    unsigned lineNumber = 0;
    auto settings = make<Settings>();

    // IBM PC's boot at this location, which usually contains a JMP to the
    // BIOS entry point.
    settings->m_entryCS = 0xF000;
    settings->m_entryIP = 0xFFF0;

    static QRegExp whitespaceRegExp("\\s");

    while (!file.atEnd()) {
        QString line = QString::fromLocal8Bit(file.readLine());
        lineNumber++;

        if (line.startsWith(QLatin1Char('#')))
            continue;

        QStringList arguments = line.split(whitespaceRegExp, QString::SkipEmptyParts);

        if (arguments.isEmpty())
            continue;

        QString command = arguments.takeFirst();

        bool success = false;

        if (command == QLatin1String("load-file"))
            success = settings->handleLoadFile(arguments);
        else if (command == QLatin1String("rom-image"))
            success = settings->handleROMImage(arguments);
        else if (command == QLatin1String("memory-size"))
            success = settings->handleMemorySize(arguments);
        else if (command == QLatin1String("fixed-disk"))
            success = settings->handleFixedDisk(arguments);
        else if (command == QLatin1String("floppy-disk"))
            success = settings->handleFloppyDisk(arguments);
        else if (command == QLatin1String("keymap"))
            success = settings->handleKeymap(arguments);

        if (!success) {
            vlog(LogConfig, "Failed parsing %s:%u %s", qPrintable(fileName), lineNumber, qPrintable(line));
            return 0;
        }
    }

    return settings;
}
