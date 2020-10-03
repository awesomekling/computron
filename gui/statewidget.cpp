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

#include "statewidget.h"

#include "CPU.h"
#include "debug.h"
#include "machine.h"
#include "screen.h"
#include "ui_statewidget.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QTime>
#include <QtCore/QTimer>
#include <QtWidgets/QVBoxLayout>

struct StateWidget::Private {
    QTimer syncTimer;
    Ui_StateWidget ui;

    u64 cycleCount { 0 };
    QTime cycleTimer;
};

StateWidget::StateWidget(Machine& m)
    : QWidget(nullptr)
    , m_machine(m)
    , d(make<Private>())
{
    setFixedSize(220, 400);
    d->ui.setupUi(this);

    connect(&d->syncTimer, SIGNAL(timeout()), this, SLOT(sync()));
    d->syncTimer.start(300);

    d->cycleTimer.start();
}

StateWidget::~StateWidget()
{
}

#define DO_LABEL(name, fmt) d->ui.lbl##name->setText(s.sprintf(fmt, cpu.get##name()));

#define DO_LABEL_N(name, getterName, title, fmt)                         \
    do {                                                                 \
        d->ui.lblTitle##name->setText(title);                            \
        d->ui.lbl##name->setText(s.sprintf(fmt, cpu.get##getterName())); \
    } while (0);

void StateWidget::sync()
{
    QString s;
    auto& cpu = machine().cpu();

    if (cpu.x32()) {
        DO_LABEL_N(EBX, EBX, "ebx", "%08x");
        DO_LABEL_N(EAX, EAX, "eax", "%08x");
        DO_LABEL_N(ECX, ECX, "ecx", "%08x");
        DO_LABEL_N(EDX, EDX, "edx", "%08x");
        DO_LABEL_N(EBP, EBP, "ebp", "%08x");
        DO_LABEL_N(ESP, ESP, "esp", "%08x");
        DO_LABEL_N(ESI, ESI, "esi", "%08x");
        DO_LABEL_N(EDI, EDI, "edi", "%08x");
        d->ui.lblPC->setText(s.sprintf("%04X:%08X", cpu.getBaseCS(), cpu.currentBaseInstructionPointer()));
    } else {
        DO_LABEL_N(EBX, BX, "bx", "%04x");
        DO_LABEL_N(EAX, AX, "ax", "%04x");
        DO_LABEL_N(ECX, CX, "cx", "%04x");
        DO_LABEL_N(EDX, DX, "dx", "%04x");
        DO_LABEL_N(EBP, BP, "bp", "%04x");
        DO_LABEL_N(ESP, SP, "sp", "%04x");
        DO_LABEL_N(ESI, SI, "si", "%04x");
        DO_LABEL_N(EDI, DI, "di", "%04x");
        d->ui.lblPC->setText(s.sprintf("%04X:%04X", cpu.getBaseCS(), cpu.getBaseIP()));
    }
    DO_LABEL(CS, "%04x");
    DO_LABEL(DS, "%04x");
    DO_LABEL(ES, "%04x");
    DO_LABEL(SS, "%04x");
    DO_LABEL(FS, "%04x");
    DO_LABEL(GS, "%04x");
    DO_LABEL(CR0, "%08x");
    DO_LABEL(CR3, "%08x");

#define DO_FLAG(getterName, name) flagString += QString("<font color='%1'>%2</font> ").arg(cpu.get##getterName() ? "black" : "#ccc").arg(name);

    QString flagString;
    DO_FLAG(OF, "of");
    DO_FLAG(SF, "sf");
    DO_FLAG(ZF, "zf");
    DO_FLAG(AF, "af");
    DO_FLAG(PF, "pf");
    DO_FLAG(CF, "cf");
    DO_FLAG(IF, "if");
    DO_FLAG(TF, "tf");
    DO_FLAG(NT, "nt");

    d->ui.lblFlags->setText(flagString);

    d->ui.lblSizes->setText(QString("a%1o%2x%3s%4").arg(cpu.a16() ? 16 : 32).arg(cpu.o16() ? 16 : 32).arg(cpu.x16() ? 16 : 32).arg(cpu.s16() ? 16 : 32));

    auto cpuCycles = cpu.cycle();
    auto cycles = cpuCycles - d->cycleCount;
    double elapsed = d->cycleTimer.elapsed() / 1000.0;
    double ips = cycles / elapsed;
    d->ui.lblIPS->setText(QString("%1").arg((u64)ips));
    d->cycleCount = cpuCycles;
    d->cycleTimer.start();
}
