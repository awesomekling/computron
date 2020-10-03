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

//#define DEBUG_JUMPS
//#define LOG_FAR_JUMPS
//#define DEBUG_TASK_SWITCH
//#define DISASSEMBLE_EVERYTHING
//#define DEBUG_VM86
//#define VMM_TRACING
//#define SYMBOLIC_TRACING

#include "types.h"
#include <QString>

#define CRASH() __builtin_trap()
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#define NEVER_INLINE __attribute__((__noinline__))
#define FLATTEN __attribute__((__flatten__))
#define PURE __attribute__((pure))
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define UNUSED_PARAM(x) (void)(x)

#define MAX_FN_LENGTH 128

void hard_exit(int exitCode);

struct RuntimeOptions {
    bool trace { false };
    bool disklog { false };
    bool trapint { false };
    bool iopeek { false };
    bool start_in_debug { false };
    bool memdebug { false };
    bool vgadebug { false };
    bool novlog { false };
    bool pedebug { false };
    bool vlogcycle { false };
    bool crashOnPF { false };
    bool crashOnGPF { false };
    bool crashOnException { false };
    bool stacklog { false };
    QString autotestPath;
    QString configPath;
#ifdef DISASSEMBLE_EVERYTHING
    bool disassembleEverything { false };
#endif
#ifdef DEBUG_SERENITY
    bool serenity { false };
#endif
    bool log_exceptions { true };
    bool log_page_translations { false };
};

extern RuntimeOptions options;

inline PhysicalAddress realModeAddressToPhysicalAddress(u16 segment, u32 offset)
{
    return PhysicalAddress((segment << 4) + offset);
}

inline void write16ToPointer(u16* pointer, u16 value)
{
#ifdef CT_BIG_ENDIAN
    *pointer = V_BYTESWAP(value);
#else
    *pointer = value;
#endif
}

inline u32 read32FromPointer(u32* pointer)
{
#ifdef CT_BIG_ENDIAN
    return V_BYTESWAP(*pointer);
#else
    return *pointer;
#endif
}

inline u16 read16FromPointer(u16* pointer)
{
#ifdef CT_BIG_ENDIAN
    return V_BYTESWAP(*pointer);
#else
    return *pointer;
#endif
}
