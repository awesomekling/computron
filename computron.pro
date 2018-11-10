macx:QMAKE_MAC_SDK = macosx10.9
CONFIG += debug_and_release
TEMPLATE = app
TARGET = computron
DEPENDPATH += . x86 bios gui hw include
INCLUDEPATH += . include gui hw x86 ../Serenity
QMAKE_CXXFLAGS += -std=c++17 -g -W -Wall -Wimplicit-fallthrough -fno-rtti

QMAKE_CXXFLAGS_RELEASE += -O3
QMAKE_CXXFLAGS_DEBUG += -O0

CONFIG += c++1z

DEFINES += DEBUG_SERENITY

DEFINES += CT_TRACE
//DEFINES += CT_DETERMINISTIC
CONFIG += silent
CONFIG += debug
QT += widgets

CONFIG -= app_bundle

unix {
    LIBS += -lreadline
    DEFINES += HAVE_READLINE
    DEFINES += HAVE_USLEEP
}

OBJECTS_DIR = .obj
RCC_DIR = .rcc
MOC_DIR = .moc
UI_DIR = .ui

RESOURCES = computron.qrc

FORMS += gui/statewidget.ui

OTHER_FILES += bios/bios.asm

HEADERS += gui/machinewidget.h \
           gui/statewidget.h \
           gui/mainwindow.h \
           gui/palettewidget.h \
           gui/screen.h \
           gui/worker.h \
           gui/Renderer.h \
           hw/MemoryProvider.h \
           hw/ROM.h \
           hw/SimpleMemoryProvider.h \
           hw/DiskDrive.h \
           hw/fdc.h \
           hw/ide.h \
           hw/iodevice.h \
           hw/keyboard.h \
           hw/vomctl.h \
           hw/cmos.h \
           hw/pic.h \
           hw/pit.h \
           hw/vga.h \
           hw/PS2.h \
           hw/busmouse.h \
           hw/MouseObserver.h \
           hw/ThreadedTimer.h \
           include/debugger.h \
           include/types.h \
           include/debug.h \
           include/machine.h \
           include/settings.h \
           include/templates.h \
           include/Common.h \
           include/OwnPtr.h \
           x86/CPU.h \
           x86/Descriptor.h \
           x86/Instruction.h \
           x86/Tasking.h

SOURCES += debug.cpp \
           debugger.cpp \
           dump.cpp \
           machine.cpp \
           settings.cpp \
           vmcalls.cpp \
           x86/bcd.cpp \
           x86/bitwise.cpp \
           x86/CPU.cpp \
           x86/Descriptor.cpp \
           x86/flags.cpp \
           x86/fpu.cpp \
           x86/Instruction.cpp \
           x86/interrupt.cpp \
           x86/io.cpp \
           x86/jump.cpp \
           x86/math.cpp \
           x86/modrm.cpp \
           x86/mov.cpp \
           x86/pmode.cpp \
           x86/stack.cpp \
           x86/string.cpp \
           x86/Tasking.cpp \
           gui/machinewidget.cpp \
           gui/main.cpp \
           gui/mainwindow.cpp \
           gui/palettewidget.cpp \
           gui/statewidget.cpp \
           gui/screen.cpp \
           gui/worker.cpp \
           gui/Renderer.cpp \
           hw/busmouse.cpp \
           hw/fdc.cpp \
           hw/ide.cpp \
           hw/keyboard.cpp \
           hw/pic.cpp \
           hw/pit.cpp \
           hw/vga.cpp \
           hw/vomctl.cpp \
           hw/iodevice.cpp \
           hw/cmos.cpp \
           hw/PS2.cpp \
           hw/MemoryProvider.cpp \
           hw/ROM.cpp \
           hw/SimpleMemoryProvider.cpp \
           hw/DiskDrive.cpp \
           hw/MouseObserver.cpp \
           hw/ThreadedTimer.cpp
