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

#include "OwnPtr.h"
#include "iodevice.h"

class FDC final : public IODevice {
public:
    explicit FDC(Machine&);
    virtual ~FDC();

    virtual void reset() override;
    virtual u8 in8(u16 port) override;
    virtual void out8(u16 port, u8 data) override;

private:
    enum ResetSource {
        Software,
        Hardware
    };
    enum class DataDirection {
        FromFDC = 0x40,
        ToFDC = 0,
        Mask = 0x40
    };
    void set_data_direction(DataDirection);
    DataDirection data_direction() const;
    bool using_dma() const;
    void set_using_dma(bool);
    void reset_controller(ResetSource);
    void reset_controller_soon();
    void generate_fdc_interrupt(bool seek_completed = false);
    void update_status(bool seek_completed = false);

    void execute_command_soon();
    void execute_command();
    void execute_command_internal();
    void execute_read_data_command();

    struct Private;
    OwnPtr<Private> d;
};
