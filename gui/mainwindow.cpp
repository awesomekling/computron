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

#include "mainwindow.h"
#include "CPU.h"
#include "keyboard.h"
#include "machine.h"
#include "machinewidget.h"
#include "screen.h"
#include <QLabel>
#include <QStatusBar>
#include <QTime>
#include <QTimer>

struct MainWindow::Private {
    Keyboard* keyboard;
    QStatusBar* status_bar;
    QLabel* message_label;
    QLabel* scroll_lock_label;
    QLabel* num_lock_label;
    QLabel* caps_lock_label;
    QTimer ips_timer;
    u64 cycle_count { 0 };
    QTime cycle_timer;
    CPU* cpu { nullptr };
};

MainWindow::MainWindow()
    : d(make<Private>())
{
    setWindowTitle("Computron");
    connect(&d->ips_timer, SIGNAL(timeout()), this, SLOT(update_ips()));
    d->ips_timer.start(500);
}

MainWindow::~MainWindow()
{
}

void MainWindow::add_machine(Machine* machine)
{
    d->cpu = &machine->cpu();

    MachineWidget* machine_widget = new MachineWidget(*machine);
    setCentralWidget(machine_widget);
    setFocusProxy(machine_widget);

    d->keyboard = &machine->keyboard();

    d->status_bar = new QStatusBar;
    setStatusBar(d->status_bar);

    d->scroll_lock_label = new QLabel("SCRL");
    d->num_lock_label = new QLabel("NUM");
    d->caps_lock_label = new QLabel("CAPS");

    d->scroll_lock_label->setAutoFillBackground(true);
    d->num_lock_label->setAutoFillBackground(true);
    d->caps_lock_label->setAutoFillBackground(true);

    on_leds_changed(0);

    connect(d->keyboard, SIGNAL(leds_changed(int)), this, SLOT(on_leds_changed(int)), Qt::QueuedConnection);

    d->message_label = new QLabel;
    d->status_bar->addWidget(d->message_label, 1);
    d->status_bar->addWidget(d->caps_lock_label);
    d->status_bar->addWidget(d->num_lock_label);
    d->status_bar->addWidget(d->scroll_lock_label);
}

void MainWindow::on_leds_changed(int leds)
{
    QPalette palette_for_led[2];
    palette_for_led[0] = d->scroll_lock_label->palette();
    palette_for_led[1] = d->scroll_lock_label->palette();
    palette_for_led[0].setColor(d->scroll_lock_label->backgroundRole(), Qt::gray);
    palette_for_led[1].setColor(d->scroll_lock_label->backgroundRole(), Qt::green);

    bool scrollLock = leds & Keyboard::LED::ScrollLock;
    bool numLock = leds & Keyboard::LED::NumLock;
    bool capsLock = leds & Keyboard::LED::CapsLock;

    d->scroll_lock_label->setPalette(palette_for_led[scrollLock]);
    d->num_lock_label->setPalette(palette_for_led[numLock]);
    d->caps_lock_label->setPalette(palette_for_led[capsLock]);
}

void MainWindow::update_ips()
{
    if (!d->cpu)
        return;
    auto cpu_cycles = d->cpu->cycle();
    auto cycles = cpu_cycles - d->cycle_count;
    double elapsed = d->cycle_timer.elapsed() / 1000.0;
    double ips = cycles / elapsed;
    d->message_label->setText(QString("Op/s: %1").arg((u64)ips));
    d->cycle_count = cpu_cycles;
    d->cycle_timer.start();
}
