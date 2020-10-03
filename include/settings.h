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
    static OwnPtr<Settings> create_from_file(const QString&);
    static OwnPtr<Settings> create_for_autotest(const QString& fileName);

    unsigned memory_size() const { return m_memory_size; }
    void set_memory_size(unsigned size) { m_memory_size = size; }

    u16 entry_cs() const { return m_entryCS; }
    u16 entry_ip() const { return m_entryIP; }
    u16 entry_ds() const { return m_entryDS; }
    u16 entry_ss() const { return m_entrySS; }
    u16 entry_sp() const { return m_entrySP; }

    const QHash<u32, QString>& files() const { return m_files; }
    const QHash<u32, QString>& rom_images() const { return m_rom_images; }
    QString keymap() const { return m_keymap; }

    bool is_for_autotest() const { return m_for_autotest; }
    void set_for_autotest(bool b) { m_for_autotest = b; }

    Settings() { }
    ~Settings() { }

    const DiskDrive::Configuration& floppy0() const { return m_floppy0; }
    const DiskDrive::Configuration& floppy1() const { return m_floppy1; }
    const DiskDrive::Configuration& fixed0() const { return m_fixed0; }
    const DiskDrive::Configuration& fixed1() const { return m_fixed1; }

private:
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    bool handle_rom_image(const QStringList&);
    bool handle_load_file(const QStringList&);
    bool handle_memory_size(const QStringList&);
    bool handle_fixed_disk(const QStringList&);
    bool handle_floppy_disk(const QStringList&);
    bool handle_keymap(const QStringList&);

    DiskDrive::Configuration m_floppy0;
    DiskDrive::Configuration m_floppy1;
    DiskDrive::Configuration m_fixed0;
    DiskDrive::Configuration m_fixed1;

    QHash<u32, QString> m_files;
    QHash<u32, QString> m_rom_images;
    QString m_keymap;
    unsigned m_memory_size { 0 };
    u16 m_entryCS { 0 };
    u16 m_entryIP { 0 };
    u16 m_entryDS { 0 };
    u16 m_entrySS { 0 };
    u16 m_entrySP { 0 };
    bool m_for_autotest { false };
};
