// Computron x86 PC Emulator
// Copyright (C) 2003-2019 Andreas Kling <awesomekling@gmail.com>
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

#include "MemoryProvider.h"
#include "OwnPtr.h"
#include "iodevice.h"
#include <QtCore/QObject>
#include <QtGui/QColor>

class VGA final : public QObject
    , public IODevice
    , public MemoryProvider {
    Q_OBJECT
public:
    explicit VGA(Machine&);
    virtual ~VGA();

    // IODevice
    virtual void reset() override;
    virtual u8 in8(u16 port) override;
    virtual void out8(u16 port, u8 data) override;

    // MemoryProvider
    virtual void writeMemory8(u32 address, u8 value) override;
    virtual u8 readMemory8(u32 address) override;

    const u8* plane(int index) const;
    const u8* text_memory() const;

    void setPaletteDirty(bool);
    bool isPaletteDirty();

    u8 readRegister(u8 index) const;

    u16 cursor_location() const;
    u8 cursor_start_scanline() const;
    u8 cursor_end_scanline() const;
    bool cursor_enabled() const;

    QColor color(int index) const;
    QColor paletteColor(int paletteIndex) const;

    u8 currentVideoMode() const;

    u16 start_address() const;

    void willRefreshScreen();
    void didRefreshScreen();

    bool inChain4Mode() const;

    void dump();

signals:
    void paletteChanged();

private:
    void synchronizeColors();
    u8 read_mode() const;
    u8 write_mode() const;
    u8 rotate_count() const;
    u8 logical_op() const;
    u8 bit_mask() const;
    u8 read_map_select() const;

    struct Private;
    OwnPtr<Private> d;
};
