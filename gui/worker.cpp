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

#include "worker.h"
#include "machine.h"
#include "CPU.h"

Worker::Worker(Machine& machine)
    : QThread(nullptr)
    , m_machine(machine)
{
}

Worker::~Worker()
{
}

void Worker::run()
{
    m_machine.makeDevices(Badge<Worker>());
    m_machine.m_workerWaiter.wakeAll();
    while (true) {
        m_machine.cpu().mainLoop();
        msleep(50);
    }
}

void Worker::shutdown()
{
    // FIXME: Implement shutdown
    hard_exit(0);
}

void Worker::exitDebugger()
{
    m_machine.cpu().queueCommand(CPU::ExitDebugger);
}

void Worker::enterDebugger()
{
    m_machine.cpu().queueCommand(CPU::EnterDebugger);
}

void Worker::rebootMachine()
{
    m_machine.cpu().queueCommand(CPU::HardReboot);
}
