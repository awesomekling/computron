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

#include "machinewidget.h"

#include "DiskDrive.h"
#include "debug.h"
#include "machine.h"
#include "palettewidget.h"
#include "screen.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>
#include <QtWidgets/QAction>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QVBoxLayout>

struct MachineWidget::Private {
    QToolBar* toolBar;

    QAction* startMachine;
    QAction* pauseMachine;
    QAction* stopMachine;
    QAction* rebootMachine;

    QTimer syncTimer;
};

MachineWidget::MachineWidget(Machine& m)
    : QWidget(0)
    , m_machine(m)
    , d(make<Private>())
{
#if 0
    // FIXME: Find a way to put this in the UI. Dock widget?
    PaletteWidget* paletteWidget = new PaletteWidget(m);
    paletteWidget->show();
#endif

    m_screen = make<Screen>(m);
    m_machine.set_widget(this);

    d->toolBar = new QToolBar(tr("Virtual Machine"));

    QVBoxLayout* layout = new QVBoxLayout;
    layout->setSpacing(0);
    layout->setMargin(0);

    layout->addWidget(d->toolBar);

    QHBoxLayout* hLayout = new QHBoxLayout;
    hLayout->setSpacing(0);
    hLayout->setMargin(0);
    hLayout->addWidget(&screen());

    layout->addLayout(hLayout);

    setLayout(layout);

    setFocusProxy(&screen());

    QAction* chooseFloppyAImage = new QAction(QIcon(":/icons/toolbar-floppy.svg"), tr("Floppy A:"), this);
    QAction* chooseFloppyBImage = new QAction(QIcon(":/icons/toolbar-floppy.svg"), tr("Floppy B:"), this);

    d->pauseMachine = new QAction(QIcon(":/icons/toolbar-pause.svg"), tr("Pause"), this);
    d->startMachine = new QAction(QIcon(":/icons/toolbar-start.svg"), tr("Start"), this);
    d->stopMachine = new QAction(QIcon(":/icons/toolbar-stop.svg"), tr("Stop"), this);
    d->rebootMachine = new QAction(QIcon(":/icons/toolbar-reset.svg"), tr("Reboot"), this);

    d->startMachine->setEnabled(false);
    d->pauseMachine->setEnabled(true);
    d->stopMachine->setEnabled(true);

    d->toolBar->addAction(d->startMachine);
    d->toolBar->addAction(d->pauseMachine);
    d->toolBar->addAction(d->stopMachine);
    d->toolBar->addAction(d->rebootMachine);

    d->toolBar->addSeparator();

    d->toolBar->addAction(chooseFloppyAImage);
    d->toolBar->addAction(chooseFloppyBImage);

    connect(chooseFloppyAImage, SIGNAL(triggered(bool)), SLOT(onFloppyATriggered()));
    connect(chooseFloppyBImage, SIGNAL(triggered(bool)), SLOT(onFloppyBTriggered()));
    connect(d->rebootMachine, SIGNAL(triggered(bool)), SLOT(onRebootTriggered()));
    connect(d->pauseMachine, SIGNAL(triggered(bool)), SLOT(onPauseTriggered()));
    connect(d->startMachine, SIGNAL(triggered(bool)), SLOT(onStartTriggered()));
    connect(d->stopMachine, SIGNAL(triggered(bool)), SLOT(onStopTriggered()));

    QObject::connect(&d->syncTimer, SIGNAL(timeout()), &screen(), SLOT(flush_key_buffer()));
    d->syncTimer.start(10);

    QObject::connect(qApp, SIGNAL(aboutToQuit()), &machine(), SLOT(stop()));
}

MachineWidget::~MachineWidget()
{
}

void MachineWidget::onFloppyATriggered()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Choose floppy A image"));
    if (fileName.isNull())
        return;
    machine().floppy0().set_image_path(fileName);
    vlog(LogDisk, "floppy0 image changed to %s", qPrintable(fileName));
}

void MachineWidget::onFloppyBTriggered()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Choose floppy B image"));
    if (fileName.isNull())
        return;
    machine().floppy1().set_image_path(fileName);
    vlog(LogDisk, "floppy1 image changed to %s", qPrintable(fileName));
}

void MachineWidget::onPauseTriggered()
{
    d->pauseMachine->setEnabled(false);
    d->startMachine->setEnabled(true);
    d->stopMachine->setEnabled(true);

    machine().pause();
}

void MachineWidget::onStopTriggered()
{
    d->pauseMachine->setEnabled(false);
    d->startMachine->setEnabled(true);
    d->stopMachine->setEnabled(false);

    machine().stop();
}

void MachineWidget::onStartTriggered()
{
    d->pauseMachine->setEnabled(true);
    d->startMachine->setEnabled(false);
    d->stopMachine->setEnabled(true);

    machine().start();
}

void MachineWidget::onRebootTriggered()
{
    machine().reboot();
}
