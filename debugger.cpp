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

#include "debugger.h"
#include "CPU.h"
#include "Common.h"
#include "debug.h"
#include "machine.h"
#include "pic.h"
#include "vga.h"
#include <QDebug>
#include <QLatin1Literal>
#include <QStringBuilder>
#include <QStringList>
#ifdef HAVE_EDITLINE
#    include <editline/history.h>
#    include <editline/readline.h>
#endif

Debugger::Debugger(CPU& cpu)
    : m_cpu(cpu)
{
}

Debugger::~Debugger()
{
}

void Debugger::enter()
{
    m_active = true;
    cpu().recompute_main_loop_needs_slow_stuff();
}

void Debugger::exit()
{
    m_active = false;
    cpu().recompute_main_loop_needs_slow_stuff();
}

static QString do_prompt(const CPU& cpu)
{
    static QString bright_magenta("\033[35;1m");
    static QString bright_cyan("\033[34;1m");
    static QString default_color("\033[0m");

    QString s;

    if (cpu.get_pe())
        s.sprintf("%04X:%08X", cpu.get_cs(), cpu.get_eip());
    else
        s.sprintf("%04X:%04X", cpu.get_cs(), cpu.get_ip());

    QString prompt = bright_magenta % QLatin1Literal("CT ") % bright_cyan % s % default_color % QLatin1Literal("> ");

#ifdef HAVE_EDITLINE
    char* line = readline(prompt.toLatin1().constData());
#else
    char* line = (char*)malloc(1024 * sizeof(char));
    fgets(line, 1024, stdin);
#endif

    QString command = line ? QString::fromUtf8(line) : QString::fromLatin1("end-of-file");
    free(line);
    return command;
}

void Debugger::handle_command(const QString& raw_command)
{
    QStringList arguments = raw_command.split(QChar(' '), QString::SkipEmptyParts);

    if (arguments.isEmpty())
        return;

#ifdef HAVE_EDITLINE
    add_history(qPrintable(raw_command));
#endif

    QString command = arguments.takeFirst();
    QString lower_command = command.toLower();

    if (lower_command == "xl") {
        if (arguments.size() != 1) {
            printf("usage: xl <address>\n");
        } else {
            u32 address = arguments.at(0).toUInt(0, 16);
            u32 dir = (address >> 22) & 0x3FF;
            u32 page = (address >> 12) & 0x3FF;
            u32 offset = address & 0xFFF;

            printf("CR3: %08x\n", cpu().get_cr3());

            printf("%08x { dir=%03x, page=%03x, offset=%03x }\n", address, dir, page, offset);

            PhysicalAddress pde_address(cpu().get_cr3() + dir * sizeof(u32));
            u32 page_directory_entry = cpu().read_physical_memory<u32>(pde_address);
            PhysicalAddress pte_address((page_directory_entry & 0xfffff000) + page * sizeof(u32));
            u32 page_table_entry = cpu().read_physical_memory<u32>(pte_address);

            printf("PDE: %08x @ %08x\n", page_directory_entry, pde_address.get());
            printf("PTE: %08x @ %08x\n", page_table_entry, pte_address.get());

            PhysicalAddress paddr((page_table_entry & 0xfffff000) | offset);
            printf("Physical: %08x\n", paddr.get());
        }
        return;
    }

    if (lower_command == "q" || lower_command == "quit" || lower_command == "end-of-file")
        return handle_quit();

    if (lower_command == "r" || lower_command == "dump-registers")
        return handle_dump_registers();

    if (lower_command == "i" || lower_command == "dump-ivt")
        return handle_dump_ivt();

    if (lower_command == "reconf")
        return handle_reconfigure();

    if (lower_command == "t" || lower_command == "tracing")
        return handle_tracing(arguments);

    if (lower_command == "s" || lower_command == "step")
        return handle_step();

    if (lower_command == "c" || lower_command == "continue")
        return handle_continue();

    if (lower_command == "d" || lower_command == "dump-memory")
        return handle_dump_memory(arguments);

    if (lower_command == "u")
        return handle_dump_unassembled(arguments);

    if (lower_command == "seg")
        return handle_dump_segment(arguments);

    if (lower_command == "m")
        return handle_dump_flat_memory(arguments);

    if (lower_command == "b")
        return handle_breakpoint(arguments);

    if (lower_command == "sel")
        return handle_selector(arguments);

    if (lower_command == "k" || lower_command == "stack")
        return handle_stack(arguments);

    if (lower_command == "gdt") {
        cpu().dump_gdt();
        return;
    }

    if (lower_command == "ldt") {
        cpu().dump_ldt();
        return;
    }

    if (lower_command == "sti") {
        vlog(LogDump, "IF <- 1");
        cpu().set_if(1);
        return;
    }

    if (lower_command == "cli") {
        vlog(LogDump, "IF <- 0");
        cpu().set_if(0);
        return;
    }

    if (lower_command == "stz") {
        vlog(LogDump, "ZF <- 1");
        cpu().set_zf(1);
        return;
    }

    if (lower_command == "clz") {
        vlog(LogDump, "ZF <- 0");
        cpu().set_zf(0);
        return;
    }

    if (lower_command == "stc") {
        vlog(LogDump, "CF <- 1");
        cpu().set_cf(1);
        return;
    }

    if (lower_command == "clc") {
        vlog(LogDump, "CF <- 0");
        cpu().set_cf(0);
        return;
    }

    if (lower_command == "unhlt") {
        cpu().set_state(CPU::Alive);
        return;
    }

    if (lower_command == "irq")
        return handle_irq(arguments);

    if (lower_command == "picmasks") {
        cpu().machine().master_pic().dump_mask();
        cpu().machine().slave_pic().dump_mask();
        return;
    }

    if (lower_command == "unmask") {
        cpu().machine().master_pic().unmask_all();
        cpu().machine().slave_pic().unmask_all();
        return;
    }

    if (lower_command == "slon") {
        options.stacklog = true;
        return;
    }

    if (lower_command == "sloff") {
        options.stacklog = false;
        return;
    }

    if (lower_command == "pt1") {
        options.log_page_translations = true;
        return;
    }

    if (lower_command == "pt0") {
        options.log_page_translations = false;
        return;
    }

    if (lower_command == "vga") {
        cpu().machine().vga().dump();
        return;
    }

#ifdef DISASSEMBLE_EVERYTHING
    if (lowerCommand == "de1") {
        options.disassemble_everything = true;
        return;
    }
    if (lowerCommand == "de0") {
        options.disassemble_everything = false;
        return;
    }
#endif

    printf("Unknown command: %s\n", command.toUtf8().constData());
}

void Debugger::handle_irq(const QStringList& arguments)
{
    if (arguments.size() != 1)
        goto usage;

    if (arguments[0] == "off") {
        printf("Ignoring all IRQs\n");
        PIC::set_ignore_all_irqs(true);
        return;
    }

    if (arguments[0] == "on") {
        printf("Allowing all IRQs\n");
        PIC::set_ignore_all_irqs(false);
        return;
    }

usage:
    printf("usage: irq <on|off>\n");
}

void Debugger::handle_breakpoint(const QStringList& arguments)
{
    if (arguments.size() < 2) {
        printf("usage: b <add|del> [segment] <offset>\n");
        if (!cpu().breakpoints().empty()) {
            printf("\nCurrent breakpoints:\n");
            for (auto& breakpoint : cpu().breakpoints()) {
                printf("    %04x:%08x\n", breakpoint.selector(), breakpoint.offset());
            }
            printf("\n");
        }
        return;
    }
    u16 selector;
    int offset_index;
    if (arguments.size() == 3) {
        selector = arguments.at(1).toUInt(0, 16);
        offset_index = 2;
    } else {
        selector = cpu().get_cs();
        offset_index = 1;
    }
    bool ok;
    u32 offset = arguments.at(offset_index).toUInt(&ok, 16);

    if (!ok) {
#ifdef SYMBOLIC_TRACING
        auto it = cpu().m_symbols_reverse.find(arguments.at(offset_index));
        if (it != cpu().m_symbols_reverse.end()) {
            offset = *it;
        } else {
#endif
            printf("invalid breakpoint '%s'\n", qPrintable(arguments.at(offset_index)));
            return;
#ifdef SYMBOLIC_TRACING
        }
#endif
    }

    LogicalAddress address(selector, offset);
    if (arguments[0] == "add") {
        printf("add breakpoint: %04x:%08x\n", selector, offset);
        cpu().breakpoints().insert(address);
    }
    if (arguments[0] == "del") {
        printf("delete breakpoint: %04x:%08x\n", selector, offset);
        cpu().breakpoints().erase(address);
    }
    cpu().recompute_main_loop_needs_slow_stuff();
}

void Debugger::do_console()
{
    ASSERT(is_active());

    printf("\n");
    cpu().dump_all();
    printf(">>> Entering Computron debugger @ %04x:%08x\n", cpu().get_base_cs(), cpu().current_base_instruction_pointer());

    while (is_active()) {
        QString raw_command = do_prompt(cpu());
        handle_command(raw_command);
    }
}

void Debugger::handle_quit()
{
    hard_exit(0);
}

void Debugger::handle_dump_registers()
{
    cpu().dump_all();
}

void Debugger::handle_dump_ivt()
{
    cpu().dump_ivt();
}

void Debugger::handle_reconfigure()
{
    // FIXME: Implement.
}

void Debugger::handle_step()
{
    cpu().execute_one_instruction();
    cpu().dump_all();
    cpu().dump_watches();
    vlog(LogDump, "Next instruction:");
    cpu().dump_disassembled(cpu().cached_descriptor(SegmentRegisterIndex::CS), cpu().get_eip());
}

void Debugger::handle_continue()
{
    exit();
}

void Debugger::handle_selector(const QStringList& arguments)
{
    if (arguments.size() == 0) {
        vlog(LogDump, "usage: sel <selector>");
        return;
    }
    u16 select = arguments.at(0).toUInt(0, 16);
    cpu().dump_descriptor(cpu().get_descriptor(select));
}

void Debugger::handle_stack(const QStringList& arguments)
{
    UNUSED_PARAM(arguments);
    cpu().dump_stack(DWordSize, 16);
}

void Debugger::handle_dump_memory(const QStringList& arguments)
{
    u16 selector = cpu().get_cs();
    u32 offset = cpu().get_eip();

    if (arguments.size() == 1)
        offset = arguments.at(0).toUInt(0, 16);
    else if (arguments.size() == 2) {
        selector = arguments.at(0).toUInt(0, 16);
        offset = arguments.at(1).toUInt(0, 16);
    }

    cpu().dump_memory(LogicalAddress(selector, offset), 16);
}

void Debugger::handle_dump_unassembled(const QStringList& arguments)
{
    u16 selector = cpu().get_cs();
    u32 offset = cpu().get_eip();

    if (arguments.size() == 1)
        offset = arguments.at(0).toUInt(0, 16);
    else if (arguments.size() == 2) {
        selector = arguments.at(0).toUInt(0, 16);
        offset = arguments.at(1).toUInt(0, 16);
    }

    u32 bytesDisassembled = cpu().dump_disassembled(LogicalAddress(selector, offset), 20);
    vlog(LogDump, "Next offset: %08x", offset + bytesDisassembled);
}

void Debugger::handle_dump_segment(const QStringList& arguments)
{
    u16 segment = cpu().get_cs();

    if (arguments.size() >= 1)
        segment = arguments.at(0).toUInt(0, 16);

    cpu().dump_segment(segment);
}

void Debugger::handle_dump_flat_memory(const QStringList& arguments)
{
    u32 address = cpu().get_eip();

    if (arguments.size() == 1)
        address = arguments.at(0).toUInt(0, 16);

    cpu().dump_flat_memory(address);
}

void Debugger::handle_tracing(const QStringList& arguments)
{
    if (arguments.size() == 1) {
        unsigned value = arguments.at(0).toUInt(0, 16);
        options.trace = value != 0;
        cpu().recompute_main_loop_needs_slow_stuff();
        return;
    }

    printf("Usage: tracing <0|1>\n");
}
