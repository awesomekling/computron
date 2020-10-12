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

class CPU;
class QString;
class QStringList;

class Debugger {
public:
    explicit Debugger(CPU&);
    ~Debugger();

    CPU& cpu() { return m_cpu; }

    void enter();
    void exit();

    bool is_active() const { return m_active; }

    void do_console();

private:
    CPU& m_cpu;
    bool m_active { false };

    void handle_command(const QString&);

    void handle_quit();
    void handle_dump_registers();
    void handle_dump_segment(const QStringList&);
    void handle_dump_ivt();
    void handle_reconfigure();
    void handle_step();
    void handle_continue();
    void handle_breakpoint(const QStringList&);
    void handle_dump_memory(const QStringList&);
    void handle_dump_flat_memory(const QStringList&);
    void handle_tracing(const QStringList&);
    void handle_irq(const QStringList&);
    void handle_dump_unassembled(const QStringList&);
    void handle_selector(const QStringList&);
    void handle_stack(const QStringList&);
};
