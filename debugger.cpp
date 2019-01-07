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
#include "Common.h"
#include "debug.h"
#include "CPU.h"
#include "pic.h"
#include "machine.h"
#include "pic.h"
#include "vga.h"
#include <QDebug>
#include <QStringBuilder>
#include <QStringList>
#include <QLatin1Literal>
#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

Debugger::Debugger(CPU& cpu)
    : m_cpu(cpu)
{
#ifdef HAVE_READLINE
    using_history();
#endif
}

Debugger::~Debugger()
{
}

void Debugger::enter()
{
    m_active = true;
    cpu().recomputeMainLoopNeedsSlowStuff();
}

void Debugger::exit()
{
    m_active = false;
    cpu().recomputeMainLoopNeedsSlowStuff();
}

static QString doPrompt(const CPU& cpu)
{
    static QString brightMagenta("\033[35;1m");
    static QString brightCyan("\033[34;1m");
    static QString defaultColor("\033[0m");

    QString s;

    if (cpu.getPE())
        s.sprintf("%04X:%08X", cpu.getCS(), cpu.getEIP());
    else
        s.sprintf("%04X:%04X", cpu.getCS(), cpu.getIP());

    QString prompt = brightMagenta % QLatin1Literal("CT ") % brightCyan % s % defaultColor % QLatin1Literal("> ");

#ifdef HAVE_READLINE
    char* line = readline(prompt.toLatin1().constData());
#else
    char* line = (char*)malloc(1024 * sizeof(char));
    fgets(line, 1024, stdin);
#endif

    QString command = line ? QString::fromUtf8(line) : QString::fromLatin1("end-of-file");
    free(line);
    return command;
}

void Debugger::handleCommand(const QString& rawCommand)
{
    QStringList arguments = rawCommand.split(QChar(' '), QString::SkipEmptyParts);

    if (arguments.isEmpty())
        return;

#ifdef HAVE_READLINE
    add_history(qPrintable(rawCommand));
#endif

    QString command = arguments.takeFirst();
    QString lowerCommand = command.toLower();

    if (lowerCommand == "xl") {
        if (arguments.size() != 1) {
            printf("usage: xl <address>\n");
        } else {
            DWORD address = arguments.at(0).toUInt(0, 16);
            DWORD dir = (address >> 22) & 0x3FF;
            DWORD page = (address >> 12) & 0x3FF;
            DWORD offset = address & 0xFFF;

            printf("CR3: %08x\n", cpu().getCR3());

            printf("%08x { dir=%03x, page=%03x, offset=%03x }\n", address, dir, page, offset);

            PhysicalAddress pdeAddress(cpu().getCR3() + dir * sizeof(DWORD));
            DWORD pageDirectoryEntry = cpu().readPhysicalMemory<DWORD>(pdeAddress);
            PhysicalAddress pteAddress((pageDirectoryEntry & 0xfffff000) + page * sizeof(DWORD));
            DWORD pageTableEntry = cpu().readPhysicalMemory<DWORD>(pteAddress);

            printf("PDE: %08x @ %08x\n", pageDirectoryEntry, pdeAddress.get());
            printf("PTE: %08x @ %08x\n", pageTableEntry, pteAddress.get());

            PhysicalAddress paddr((pageTableEntry & 0xfffff000) | offset);
            printf("Physical: %08x\n", paddr.get());
        }
        return;
    }

    if (lowerCommand == "q" || lowerCommand == "quit" || lowerCommand == "end-of-file")
        return handleQuit();

    if (lowerCommand == "r" || lowerCommand == "dump-registers")
        return handleDumpRegisters();

    if (lowerCommand == "i" || lowerCommand == "dump-ivt")
        return handleDumpIVT();

    if (lowerCommand == "reconf")
        return handleReconfigure();

    if (lowerCommand == "t" || lowerCommand == "tracing")
        return handleTracing(arguments);

    if (lowerCommand == "s" || lowerCommand == "step")
        return handleStep();

    if (lowerCommand == "c" || lowerCommand == "continue")
        return handleContinue();

    if (lowerCommand == "d" || lowerCommand == "dump-memory")
        return handleDumpMemory(arguments);

    if (lowerCommand == "u")
        return handleDumpUnassembled(arguments);

    if (lowerCommand == "seg")
        return handleDumpSegment(arguments);

    if (lowerCommand == "m")
        return handleDumpFlatMemory(arguments);

    if (lowerCommand == "b")
        return handleBreakpoint(arguments);

    if (lowerCommand == "sel")
        return handleSelector(arguments);

    if (lowerCommand == "k" || lowerCommand == "stack")
        return handleStack(arguments);

    if (lowerCommand == "gdt") {
        cpu().dumpGDT();
        return;
    }

    if (lowerCommand == "ldt") {
        cpu().dumpLDT();
        return;
    }

    if (lowerCommand == "sti") {
        vlog(LogDump, "IF <- 1");
        cpu().setIF(1);
        return;
    }

    if (lowerCommand == "cli") {
        vlog(LogDump, "IF <- 0");
        cpu().setIF(0);
        return;
    }

    if (lowerCommand == "stz") {
        vlog(LogDump, "ZF <- 1");
        cpu().setZF(1);
        return;
    }

    if (lowerCommand == "clz") {
        vlog(LogDump, "ZF <- 0");
        cpu().setZF(0);
        return;
    }

    if (lowerCommand == "stc") {
        vlog(LogDump, "CF <- 1");
        cpu().setCF(1);
        return;
    }

    if (lowerCommand == "clc") {
        vlog(LogDump, "CF <- 0");
        cpu().setCF(0);
        return;
    }


    if (lowerCommand == "unhlt") {
        cpu().setState(CPU::Alive);
        return;
    }

    if (lowerCommand == "irq")
        return handleIRQ(arguments);

    if (lowerCommand == "picmasks") {
        cpu().machine().masterPIC().dumpMask();
        cpu().machine().slavePIC().dumpMask();
        return;
    }

    if (lowerCommand == "unmask") {
        cpu().machine().masterPIC().unmaskAll();
        cpu().machine().slavePIC().unmaskAll();
        return;
    }

    if (lowerCommand == "slon") {
        options.stacklog = true;
        return;
    }

    if (lowerCommand == "sloff") {
        options.stacklog = false;
        return;
    }

    if (lowerCommand == "pt1") {
        options.log_page_translations = true;
        return;
    }

    if (lowerCommand == "pt0") {
        options.log_page_translations = false;
        return;
    }

    if (lowerCommand == "vga") {
        cpu().machine().vga().dump();
        return;
    }

#ifdef DISASSEMBLE_EVERYTHING
    if (lowerCommand == "de1") {
        options.disassembleEverything = true;
        return;
    }
    if (lowerCommand == "de0") {
        options.disassembleEverything = false;
        return;
    }
#endif

    printf("Unknown command: %s\n", command.toUtf8().constData());
}

void Debugger::handleIRQ(const QStringList& arguments)
{
    if (arguments.size() != 1)
        goto usage;

    if (arguments[0] == "off") {
        printf("Ignoring all IRQs\n");
        PIC::setIgnoreAllIRQs(true);
        return;
    }

    if (arguments[0] == "on") {
        printf("Allowing all IRQs\n");
        PIC::setIgnoreAllIRQs(false);
        return;
    }

usage:
    printf("usage: irq <on|off>\n");
}

void Debugger::handleBreakpoint(const QStringList& arguments)
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
    WORD selector;
    int offset_index;
    if (arguments.size() == 3) {
        selector = arguments.at(1).toUInt(0, 16);
        offset_index = 2;
    } else {
        selector = cpu().getCS();
        offset_index = 1;
    }
    bool ok;
    DWORD offset = arguments.at(offset_index).toUInt(&ok, 16);

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
    cpu().recomputeMainLoopNeedsSlowStuff();
}

void Debugger::doConsole()
{
    ASSERT(isActive());

    printf("\n");
    cpu().dumpAll();
    printf(">>> Entering Computron debugger @ %04x:%08x\n", cpu().getBaseCS(), cpu().currentBaseInstructionPointer());

    while (isActive()) {
        QString rawCommand = doPrompt(cpu());
        handleCommand(rawCommand);
    }
}

void Debugger::handleQuit()
{
    hard_exit(0);
}

void Debugger::handleDumpRegisters()
{
    cpu().dumpAll();
}

void Debugger::handleDumpIVT()
{
    cpu().dumpIVT();
}

void Debugger::handleReconfigure()
{
    // FIXME: Implement.
}

void Debugger::handleStep()
{
    cpu().executeOneInstruction();
    cpu().dumpAll();
    cpu().dumpWatches();
    vlog(LogDump, "Next instruction:");
    cpu().dumpDisassembled(cpu().cachedDescriptor(SegmentRegisterIndex::CS), cpu().getEIP());
}

void Debugger::handleContinue()
{
    exit();
}

void Debugger::handleSelector(const QStringList& arguments)
{
    if (arguments.size() == 0) {
        vlog(LogDump, "usage: sel <selector>");
        return;
    }
    WORD select = arguments.at(0).toUInt(0, 16);
    cpu().dumpDescriptor(cpu().getDescriptor(select));
}

void Debugger::handleStack(const QStringList& arguments)
{
    UNUSED_PARAM(arguments);
    cpu().dumpStack(DWordSize, 16);
}

void Debugger::handleDumpMemory(const QStringList& arguments)
{
    WORD selector = cpu().getCS();
    DWORD offset = cpu().getEIP();

    if (arguments.size() == 1)
        offset = arguments.at(0).toUInt(0, 16);
    else if (arguments.size() == 2) {
        selector = arguments.at(0).toUInt(0, 16);
        offset = arguments.at(1).toUInt(0, 16);
    }

    cpu().dumpMemory(LogicalAddress(selector, offset), 16);
}

void Debugger::handleDumpUnassembled(const QStringList& arguments)
{
    WORD selector = cpu().getCS();
    DWORD offset = cpu().getEIP();

    if (arguments.size() == 1)
        offset = arguments.at(0).toUInt(0, 16);
    else if (arguments.size() == 2) {
        selector = arguments.at(0).toUInt(0, 16);
        offset = arguments.at(1).toUInt(0, 16);
    }

    DWORD bytesDisassembled = cpu().dumpDisassembled(LogicalAddress(selector, offset), 20);
    vlog(LogDump, "Next offset: %08x", offset + bytesDisassembled);
}

void Debugger::handleDumpSegment(const QStringList& arguments)
{
    WORD segment = cpu().getCS();

    if (arguments.size() >= 1)
        segment = arguments.at(0).toUInt(0, 16);

    cpu().dumpSegment(segment);
}

void Debugger::handleDumpFlatMemory(const QStringList& arguments)
{
    DWORD address = cpu().getEIP();

    if (arguments.size() == 1)
        address = arguments.at(0).toUInt(0, 16);

    cpu().dumpFlatMemory(address);
}

void Debugger::handleTracing(const QStringList& arguments)
{
    if (arguments.size() == 1) {
        unsigned value = arguments.at(0).toUInt(0, 16);
        options.trace = value != 0;
        cpu().recomputeMainLoopNeedsSlowStuff();
        return;
    }

    printf("Usage: tracing <0|1>\n");
}
