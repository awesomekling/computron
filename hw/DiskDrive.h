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
        QString imagePath;
        unsigned sectorsPerTrack { 0 };
        unsigned heads { 0 };
        unsigned sectors { 0 };
        unsigned bytesPerSector { 0 };
        BYTE floppyTypeForCMOS { 0 };
    };

    explicit DiskDrive(const QString& name);
    ~DiskDrive();

    QString name() const { return m_name; }
    void setConfiguration(Configuration);

    void setImagePath(const QString&);
    QString imagePath() const { return m_config.imagePath; }

    DWORD toLBA(WORD cylinder, BYTE head, WORD sector)
    {
        return (sector - 1) + (head * sectorsPerTrack()) + (cylinder * sectorsPerTrack() * heads());
    }

    bool present() const { return m_present; }
    unsigned cylinders() const { return (m_config.sectors / m_config.sectorsPerTrack / m_config.heads) - 2; }
    unsigned heads() const { return m_config.heads; }
    unsigned sectors() const { return m_config.sectors; }
    unsigned sectorsPerTrack() const { return m_config.sectorsPerTrack; }
    unsigned bytesPerSector() const { return m_config.bytesPerSector; }
    BYTE floppyTypeForCMOS() const { return m_config.floppyTypeForCMOS; }

    //private:
    Configuration m_config;
    QString m_name;
    bool m_present { false };
};
