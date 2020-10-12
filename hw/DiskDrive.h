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

#include "types.h"
#include <QString>

class DiskDrive {
public:
    struct Configuration {
        QString image_path;
        unsigned sectors_per_track { 0 };
        unsigned heads { 0 };
        unsigned sectors { 0 };
        unsigned bytes_per_sector { 0 };
        u8 floppy_type_for_cmos { 0 };
    };

    explicit DiskDrive(const QString& name);
    ~DiskDrive();

    QString name() const { return m_name; }
    void set_configuration(Configuration);

    void set_image_path(const QString&);
    QString image_path() const { return m_config.image_path; }

    u32 to_lba(u16 cylinder, u8 head, u16 sector)
    {
        return (sector - 1) + (head * sectors_per_track()) + (cylinder * sectors_per_track() * heads());
    }

    bool present() const { return m_present; }
    unsigned cylinders() const { return (m_config.sectors / m_config.sectors_per_track / m_config.heads) - 2; }
    unsigned heads() const { return m_config.heads; }
    unsigned sectors() const { return m_config.sectors; }
    unsigned sectors_per_track() const { return m_config.sectors_per_track; }
    unsigned bytes_per_sector() const { return m_config.bytes_per_sector; }
    u8 floppy_type_for_cmos() const { return m_config.floppy_type_for_cmos; }

    //private:
    Configuration m_config;
    QString m_name;
    bool m_present { false };
};
