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

struct IDEController;

class IDE final : public IODevice {
public:
    enum Status {
        ERROR = 0x01,
        INDEX = 0x02,
        CORR = 0x04,
        DRQ = 0x08,
        DSC = 0x10,
        DWF = 0x20,
        DRDY = 0x40,
        BUSY = 0x80
    };

    explicit IDE(Machine&);
    virtual ~IDE();

    virtual void reset() override;
    virtual BYTE in8(WORD port) override;
    virtual WORD in16(WORD port) override;
    virtual DWORD in32(WORD port) override;
    virtual void out8(WORD port, BYTE data) override;
    virtual void out16(WORD port, WORD data) override;
    virtual void out32(WORD port, DWORD data) override;

private:
    void executeCommand(IDEController&, BYTE);
    Status status(const IDEController&) const;

    struct Private;
    OwnPtr<Private> d;
};
