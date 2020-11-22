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

#ifndef NDEBUG
#    include <QtCore/qdebug.h>
#    include <assert.h>
#    define ASSERT assert
#    define ASSERT_NOT_REACHED() ASSERT(false)
#    define RELEASE_ASSERT assert
#else
#    define ASSERT(x)
#    define ASSERT_NOT_REACHED() CRASH()
#    define RELEASE_ASSERT(x) \
        do {                  \
            if (!(x)) {       \
                CRASH();      \
            }                 \
            while (0)
#endif

#define BEGIN_ASSERT_NO_EXCEPTIONS try {
#define END_ASSERT_NO_EXCEPTIONS \
    }                            \
    catch (...) { ASSERT_NOT_REACHED(); }

enum VLogChannel {
    LogInit,
    LogError,
    LogExit,
    LogFPU,
    LogCPU,
    LogIO,
    LogAlert,
    LogDisk,
    LogIDE,
    LogVGA,
    LogCMOS,
    LogPIC,
    LogMouse,
    LogFDC,
    LogConfig,
    LogVomCtl,
    LogKeyboard,
    LogDump,
    LogScreen,
    LogTimer,
    LogDMA,
#ifdef DEBUG_SERENITY
    LogSerenity,
#endif
};

void vlog(VLogChannel channel, const char* format, ...);
