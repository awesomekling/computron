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

#include "CPU.h"
#include "Common.h"
#include "debugger.h"
#include "iodevice.h"
#include "machine.h"
#include "mainwindow.h"
#include "screen.h"
#include "settings.h"
#include <QFile>
#include <QtWidgets/QApplication>
#include <signal.h>

static void parse_arguments(const QStringList& arguments);

RuntimeOptions options;

static void sigint_handler(int)
{
    ASSERT(g_cpu);
    g_cpu->debugger().enter();
}

void hard_exit(int exit_code)
{
    exit(exit_code);
}

int main(int argc, char** argv)
{
    OwnPtr<QCoreApplication> app;

    for (int i = 1; i < argc; ++i) {
        if (QString::fromLatin1(argv[i]) == "--no-gui") {
            app = make<QCoreApplication>(argc, argv);
            break;
        }
    }

    if (!app) {
        app = make<QApplication>(argc, argv);
        QApplication::setWindowIcon(QIcon(":/icons/computron.ico"));
    }

    parse_arguments(app->arguments());

    signal(SIGINT, sigint_handler);

    OwnPtr<Machine> machine;

    if (options.autotest_path.length()) {
        machine = Machine::create_for_autotest(options.autotest_path);
    } else if (options.config_path.length()) {
        machine = Machine::create_from_file(options.config_path);
    } else {
        machine = Machine::create_from_file(QLatin1String("default.vmf"));
    }

    if (!machine)
        return 1;

    if (options.start_in_debug)
        machine->cpu().debugger().enter();

    QFile::remove("log.txt");

    machine->for_each_io_device([](IODevice& device) {
        vlog(LogInit, "%s present", device.name());
    });

    if (machine->settings().is_for_autotest()) {
        machine->cpu().main_loop();
        return 0;
    }

    MainWindow mainWindow;
    mainWindow.add_machine(machine.ptr());
    mainWindow.show();
    mainWindow.setFocus();

    return app->exec();
}

void parse_arguments(const QStringList& arguments)
{
    for (auto it = arguments.begin(); it != arguments.end();) {
        const auto& argument = *it;
        if (argument == "--disklog")
            options.disklog = true;
#ifdef DEBUG_SERENITY
        else if (argument == "--serenity")
            options.serenity = true;
#endif
        else if (argument == "--trapint")
            options.trapint = true;
        else if (argument == "--memdebug")
            options.memdebug = true;
        else if (argument == "--vlog-cycle")
            options.vlogcycle = true;
        else if (argument == "--crash-on-pf")
            options.crash_on_page_fault = true;
        else if (argument == "--crash-on-gpf")
            options.crash_on_general_protection_fault = true;
        else if (argument == "--crash-on-exception")
            options.crash_on_exception = true;
        else if (argument == "--pedebug")
            options.pedebug = true;
        else if (argument == "--vgadebug")
            options.vgadebug = true;
        else if (argument == "--iopeek")
            options.iopeek = true;
        else if (argument == "--trace")
            options.trace = true;
        else if (argument == "--debug")
            options.start_in_debug = true;
        else if (argument == "--no-vlog")
            options.novlog = true;
        else if (argument == "--no-log-exceptions")
            options.log_exceptions = false;
        else if (argument == "--config") {
            ++it;
            if (it == arguments.end()) {
                fprintf(stderr, "usage: computron --config [filename]\n");
                hard_exit(1);
            }
            options.config_path = (*it);
            continue;
        } else if (argument == "--run") {
            ++it;
            if (it == arguments.end()) {
                fprintf(stderr, "usage: computron --run [filename]\n");
                hard_exit(1);
            }
            options.autotest_path = (*it);
            continue;
        }
        ++it;
    }

#ifndef CT_TRACE
    if (options.trace) {
        fprintf(stderr, "Rebuild with #define CT_TRACE if you want --trace to work.\n");
        hard_exit(1);
    }
#endif
}

static_assert(TypeTrivia<u8>::mask == 0xff, "TypeTrivia<u8>::mask");
static_assert(TypeTrivia<u16>::mask == 0xffff, "TypeTrivia<u16>::mask");
static_assert(TypeTrivia<u32>::mask == 0xffffffff, "TypeTrivia<u32>::mask");
static_assert(TypeTrivia<u64>::mask == 0xffffffffffffffff, "TypeTrivia<u64>::mask");
static_assert(weld<u16>(0xf0, 0x0f) == 0xf00f, "weld<u16>");
static_assert(weld<u32>(0xbeef, 0xbabe) == 0xbeefbabe, "weld<u32>");
static_assert(weld<u64>(0xcafebabe, 0xdeadbeef) == 0xcafebabedeadbeef, "weld<u64>");
static_assert(std::numeric_limits<i8>::min() == -0x80, "min i8");
static_assert(std::numeric_limits<i8>::max() == 0x7f, "max i8");
static_assert(std::numeric_limits<i16>::min() == -0x8000, "min i16");
static_assert(std::numeric_limits<i16>::max() == 0x7fff, "max i16");
static_assert(std::numeric_limits<i32>::min() == -0x80000000L, "min i32");
static_assert(std::numeric_limits<i32>::max() == 0x7fffffff, "max i32");
static_assert(signExtendedTo<i16>(u8(0x80)) == -128, "signExtendedTo<i16> -");
static_assert(signExtendedTo<i32>(u8(0x80)) == -128, "signExtendedTo<i32> -");
static_assert(signExtendedTo<i16>(u8(0x7f)) == 127, "signExtendedTo<i16> +");
static_assert(signExtendedTo<i32>(u8(0x7f)) == 127, "signExtendedTo<i32> +");
