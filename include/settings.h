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

#include "DiskDrive.h"
#include "OwnPtr.h"
#include "types.h"
#include <QtCore/QHash>
#include <QtCore/QString>

class QStringList;

class Settings {
public:
    static OwnPtr<Settings> createFromFile(const QString&);
    static OwnPtr<Settings> createForAutotest(const QString& fileName);

    unsigned memorySize() const { return m_memorySize; }
    void setMemorySize(unsigned size) { m_memorySize = size; }

    u16 entryCS() const { return m_entryCS; }
    u16 entryIP() const { return m_entryIP; }
    u16 entryDS() const { return m_entryDS; }
    u16 entrySS() const { return m_entrySS; }
    u16 entrySP() const { return m_entrySP; }

    QHash<u32, QString> files() const { return m_files; }
    QHash<u32, QString> romImages() const { return m_romImages; }
    QString keymap() const { return m_keymap; }

    bool isForAutotest() const { return m_forAutotest; }
    void setForAutotest(bool b) { m_forAutotest = b; }

    Settings() { }
    ~Settings() { }

    const DiskDrive::Configuration& floppy0() const { return m_floppy0; }
    const DiskDrive::Configuration& floppy1() const { return m_floppy1; }
    const DiskDrive::Configuration& fixed0() const { return m_fixed0; }
    const DiskDrive::Configuration& fixed1() const { return m_fixed1; }

private:
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    bool handleROMImage(const QStringList&);
    bool handleLoadFile(const QStringList&);
    bool handleMemorySize(const QStringList&);
    bool handleFixedDisk(const QStringList&);
    bool handleFloppyDisk(const QStringList&);
    bool handleKeymap(const QStringList&);

    DiskDrive::Configuration m_floppy0;
    DiskDrive::Configuration m_floppy1;
    DiskDrive::Configuration m_fixed0;
    DiskDrive::Configuration m_fixed1;

    QHash<u32, QString> m_files;
    QHash<u32, QString> m_romImages;
    QString m_keymap;
    unsigned m_memorySize { 0 };
    u16 m_entryCS { 0 };
    u16 m_entryIP { 0 };
    u16 m_entryDS { 0 };
    u16 m_entrySS { 0 };
    u16 m_entrySP { 0 };
    bool m_forAutotest { false };
};
