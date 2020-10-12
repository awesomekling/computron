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

#include "screen.h"
#include "CPU.h"
#include "Common.h"
#include "Renderer.h"
#include "busmouse.h"
#include "debug.h"
#include "keyboard.h"
#include "machine.h"
#include "settings.h"
#include "vga.h"
#include <QtCore/QDebug>
#include <QtCore/QMutex>
#include <QtCore/QQueue>
#include <QtCore/QTimer>
#include <QtGui/QBitmap>
#include <QtGui/QPaintEvent>
#include <QtGui/QPainter>

struct fontcharbitmap_t {
    u8 data[16];
};

static Screen* s_self = 0L;

struct Screen::Private {
    QMutex key_queue_lock;

    QQueue<u16> key_queue;
    QQueue<u8> raw_queue;

    QTimer refresh_timer;
    QTimer periodic_refresh_timer;

    OwnPtr<TextRenderer> text_renderer;
    OwnPtr<Mode04Renderer> mode04_renderer;
    OwnPtr<Mode0DRenderer> mode0D_renderer;
    OwnPtr<Mode12Renderer> mode12_renderer;
    OwnPtr<Mode13Renderer> mode13_renderer;
    OwnPtr<DummyRenderer> dummy_renderer;
};

Screen::Screen(Machine& m)
    : QOpenGLWidget(nullptr)
    , d(make<Private>())
    , m_machine(m)
{
    s_self = this;

    d->text_renderer = make<TextRenderer>(*this);
    d->mode04_renderer = make<Mode04Renderer>(*this);
    d->mode0D_renderer = make<Mode0DRenderer>(*this);
    d->mode12_renderer = make<Mode12Renderer>(*this);
    d->mode13_renderer = make<Mode13Renderer>(*this);
    d->dummy_renderer = make<DummyRenderer>(*this);

    init();

    setFocusPolicy(Qt::ClickFocus);

    setMouseTracking(true);

    // This timer is kicked whenever screen memory is modified.
    d->refresh_timer.setSingleShot(true);
    d->refresh_timer.setInterval(50);
    connect(&d->refresh_timer, SIGNAL(timeout()), this, SLOT(refresh()));

    // This timer does a forced refresh() every second, in case we miss anything.
    // FIXME: This would not be needed if we had perfect invalidation + scanline timing.
    d->periodic_refresh_timer.setInterval(1000);
    d->periodic_refresh_timer.start();
    connect(&d->periodic_refresh_timer, SIGNAL(timeout()), this, SLOT(refresh()));
}

Screen::~Screen()
{
}

MouseObserver& Screen::mouse_observer()
{
    return machine().busmouse();
}

void Screen::schedule_refresh()
{
    if (!d->refresh_timer.isActive())
        d->refresh_timer.start();
}

void Screen::notify()
{
    if (d->refresh_timer.isActive())
        return;
    QMetaObject::invokeMethod(this, "schedule_refresh", Qt::QueuedConnection);
}

class RefreshGuard {
public:
    RefreshGuard(Machine& machine)
        : m_machine(machine)
    {
        m_machine.vga().will_refresh_screen();
    }
    ~RefreshGuard() { m_machine.vga().did_refresh_screen(); }

private:
    Machine& m_machine;
};

inline bool is_video_mode_using_vga_memory(u8 video_mode)
{
    return video_mode == 0x0D || video_mode == 0x12 || video_mode == 0x13;
}

void Screen::refresh()
{
    RefreshGuard guard(machine());

    u8 video_mode = current_video_mode();
    bool video_mode_changed = false;

    if (m_video_mode_in_last_refresh != video_mode) {
        vlog(LogScreen, "Video mode changed to %02X", video_mode);
        m_video_mode_in_last_refresh = video_mode;
        video_mode_changed = true;
    }

    if (video_mode_changed) {
        renderer().will_become_active();
    }

    if (is_video_mode_using_vga_memory(video_mode)) {
        if (machine().vga().is_palette_dirty()) {
            renderer().synchronize_colors();
            machine().vga().set_palette_dirty(false);
        }
    }

    renderer().synchronize_font();
    renderer().synchronize_colors();
    renderer().render();

    update();
}

Renderer& Screen::renderer()
{
    switch (current_video_mode()) {
    case 0x03:
        return *d->text_renderer;
    case 0x04:
        return *d->mode04_renderer;
    case 0x0D:
        return *d->mode0D_renderer;
    case 0x12:
        return *d->mode12_renderer;
    case 0x13:
        return *d->mode13_renderer;
    default:
        return *d->dummy_renderer;
    }
}

void Screen::set_screen_size(int width, int height)
{
    if (m_width == width && m_height == height)
        return;

    m_width = width;
    m_height = height;

    setFixedSize(m_width, m_height);
}

void Screen::resizeEvent(QResizeEvent* e)
{
    QOpenGLWidget::resizeEvent(e);

    vlog(LogScreen, "Resized viewport from %dx%d to %dx%d", e->oldSize().width(), e->oldSize().height(), e->size().width(), e->size().height());
    update();
}

void Screen::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    renderer().paint(p);
}

u8 Screen::current_video_mode() const
{
    return machine().vga().current_video_mode();
}

u8 Screen::current_row_count() const
{
    // FIXME: Don't get through BDA.
    return machine().cpu().read_physical_memory<u8>(PhysicalAddress(0x484)) + 1;
}

u8 Screen::current_column_count() const
{
    // FIXME: Don't get through BDA.
    return machine().cpu().read_physical_memory<u8>(PhysicalAddress(0x44a));
}

void Screen::mouseMoveEvent(QMouseEvent* e)
{
    QOpenGLWidget::mouseMoveEvent(e);
    mouse_observer().move_event(e->x(), e->y());
}

void Screen::mousePressEvent(QMouseEvent* e)
{
    QOpenGLWidget::mousePressEvent(e);
    switch (e->button()) {
    case Qt::LeftButton:
        mouse_observer().button_press_event(e->x(), e->y(), MouseButton::Left);
        break;
    case Qt::RightButton:
        mouse_observer().button_press_event(e->x(), e->y(), MouseButton::Right);
        break;
    default:
        break;
    }
}

void Screen::mouseReleaseEvent(QMouseEvent* e)
{
    QOpenGLWidget::mouseReleaseEvent(e);
    switch (e->button()) {
    case Qt::LeftButton:
        mouse_observer().button_release_event(e->x(), e->y(), MouseButton::Left);
        break;
    case Qt::RightButton:
        mouse_observer().button_release_event(e->x(), e->y(), MouseButton::Right);
        break;
    default:
        break;
    }
}

// This sucks, any suggestions?

#include "screen.h"
#include <QDebug>
#include <QHash>
#include <QKeyEvent>
#include <QMutexLocker>

static QHash<QString, u16> normals;
static QHash<QString, u16> shifts;
static QHash<QString, u16> ctrls;
static QHash<QString, u16> alts;
static QHash<QString, u8> make_code;
static QHash<QString, u8> break_code;
static QHash<QString, bool> extended;

static void add_key(const QString& key_name, u16 normal, u16 shift, u16 ctrl, u16 alt, bool is_extended = false)
{
    normals.insert(key_name, normal);
    shifts.insert(key_name, shift);
    ctrls.insert(key_name, ctrl);
    alts.insert(key_name, alt);
    extended.insert(key_name, is_extended);
    make_code.insert(key_name, (normal & 0xFF00) >> 8);
    break_code.insert(key_name, ((normal & 0xFF00) >> 8) | 0x80);
}

bool Screen::load_keymap(const QString& filename)
{
    QFile keymapFile(filename);
    if (!keymapFile.open(QIODevice::ReadOnly))
        return false;

    while (!keymapFile.atEnd()) {
        QByteArray rawLine = keymapFile.readLine();
        QString line = QString::fromLatin1(rawLine);
        QStringList pieces = line.split(QChar(' '));

        if (line.startsWith('#'))
            continue;

        if (pieces.size() != 2)
            continue;

        bool ok;
        u8 native_key;
        if (pieces[1].startsWith("0x"))
            native_key = pieces[1].toUInt(&ok, 16);
        else
            native_key = pieces[1].toUInt(&ok);
        if (!ok) {
            printf("Invalid keymap line: '%s'\n", rawLine.data());
            continue;
        }

        //printf("Pieces: %02X => '%s'\n", nativeKey, qPrintable(pieces[0]));

        // FIXME: Check that the key name is valid.
        m_key_mappings[native_key] = pieces[0];
    }

    return true;
}

void Screen::init()
{
    make_code["LShift"] = 0x2A;
    make_code["LCtrl"] = 0x1D;
    make_code["LAlt"] = 0x38;
    make_code["RShift"] = 0x36;
    make_code["RCtrl"] = 0x1D;
    make_code["RAlt"] = 0x38;

    break_code["LShift"] = 0xAA;
    break_code["LCtrl"] = 0x9D;
    break_code["LAlt"] = 0xB8;

    break_code["RShift"] = 0xB6;
    break_code["RCtrl"] = 0x9D;
    break_code["RAlt"] = 0xB8;

    add_key("A", 0x1E61, 0x1E41, 0x1E01, 0x1E00);
    add_key("B", 0x3062, 0x3042, 0x3002, 0x3000);
    add_key("C", 0x2E63, 0x2E43, 0x2E03, 0x2E00);
    add_key("D", 0x2064, 0x2044, 0x2004, 0x2000);
    add_key("E", 0x1265, 0x1245, 0x1205, 0x1200);
    add_key("F", 0x2166, 0x2146, 0x2106, 0x2100);
    add_key("G", 0x2267, 0x2247, 0x2207, 0x2200);
    add_key("H", 0x2368, 0x2348, 0x2308, 0x2300);
    add_key("I", 0x1769, 0x1749, 0x1709, 0x1700);
    add_key("J", 0x246A, 0x244A, 0x240A, 0x2400);
    add_key("K", 0x256B, 0x254B, 0x250B, 0x2500);
    add_key("L", 0x266C, 0x264C, 0x260C, 0x2600);
    add_key("M", 0x326D, 0x324D, 0x320D, 0x3200);
    add_key("N", 0x316E, 0x314E, 0x310E, 0x3100);
    add_key("O", 0x186F, 0x184F, 0x180F, 0x1800);
    add_key("P", 0x1970, 0x1950, 0x1910, 0x1900);
    add_key("Q", 0x1071, 0x1051, 0x1011, 0x1000);
    add_key("R", 0x1372, 0x1352, 0x1312, 0x1300);
    add_key("S", 0x1F73, 0x1F53, 0x1F13, 0x1F00);
    add_key("T", 0x1474, 0x1454, 0x1414, 0x1400);
    add_key("U", 0x1675, 0x1655, 0x1615, 0x1600);
    add_key("V", 0x2F76, 0x2F56, 0x2F16, 0x2F00);
    add_key("W", 0x1177, 0x1157, 0x1117, 0x1100);
    add_key("X", 0x2D78, 0x2D58, 0x2D18, 0x2D00);
    add_key("Y", 0x1579, 0x1559, 0x1519, 0x1500);
    add_key("Z", 0x2C7A, 0x2C5A, 0x2C1A, 0x2C00);

    add_key("1", 0x0231, 0x0221, 0, 0x7800);
    add_key("2", 0x0332, 0x0340, 0x0300, 0x7900);
    add_key("3", 0x0433, 0x0423, 0, 0x7A00);
    add_key("4", 0x0534, 0x0524, 0, 0x7B00);
    add_key("5", 0x0635, 0x0625, 0, 0x7C00);
    add_key("6", 0x0736, 0x075E, 0x071E, 0x7D00);
    add_key("7", 0x0837, 0x0826, 0, 0x7E00);
    add_key("8", 0x0938, 0x092A, 0, 0x7F00);
    add_key("9", 0x0A39, 0x0a28, 0, 0x8000);
    add_key("0", 0x0B30, 0x0B29, 0, 0x8100);

    add_key("F1", 0x3B00, 0x5400, 0x5E00, 0x6800);
    add_key("F2", 0x3C00, 0x5500, 0x5F00, 0x6900);
    add_key("F3", 0x3D00, 0x5600, 0x6000, 0x6A00);
    add_key("F4", 0x3E00, 0x5700, 0x6100, 0x6B00);
    add_key("F5", 0x3F00, 0x5800, 0x6200, 0x6C00);
    add_key("F6", 0x4000, 0x5900, 0x6300, 0x6D00);
    add_key("F7", 0x4100, 0x5A00, 0x6400, 0x6E00);
    add_key("F8", 0x4200, 0x5B00, 0x6500, 0x6F00);
    add_key("F9", 0x4300, 0x5C00, 0x6600, 0x7000);
    add_key("F10", 0x4400, 0x5D00, 0x6700, 0x7100);
    add_key("F11", 0x8500, 0x8700, 0x8900, 0x8B00);
    add_key("F12", 0x8600, 0x8800, 0x8A00, 0x8C00);

    add_key("Slash", 0x352F, 0x353F, 0, 0);
    add_key("Minus", 0x0C2D, 0x0C5F, 0xC1F, 0x8200);
    add_key("Period", 0x342E, 0x343E, 0, 0);
    add_key("Comma", 0x332C, 0x333C, 0, 0);
    add_key("Semicolon", 0x273B, 0x273A, 0, 0x2700);

    add_key("LeftBracket", 0x1A5B, 0x1A7B, 0x1A1B, 0x1A00);
    add_key("RightBracket", 0x1B5D, 0x1B7D, 0x1B1D, 0x1B00);
    add_key("Apostrophe", 0x2827, 0x2822, 0, 0);
    add_key("Backslash", 0x2B5C, 0x2B7C, 0x2B1C, 0x2600);

    add_key("Tab", 0x0F09, 0x0F00, 0x9400, 0xA500);
    add_key("Backspace", 0x0E08, 0x0E08, 0x0E7F, 0x0E00);
    add_key("Return", 0x1C0D, 0x1C0D, 0x1C0A, 0xA600);
    add_key("Space", 0x3920, 0x3920, 0x3920, 0x3920);
    add_key("Escape", 0x011B, 0x011B, 0x011B, 0x0100);

    add_key("Up", 0x4800, 0x4838, 0x8D00, 0x9800, true);
    add_key("Down", 0x5000, 0x5032, 0x9100, 0xA000, true);
    add_key("Left", 0x4B00, 0x4B34, 0x7300, 0x9B00, true);
    add_key("Right", 0x4D00, 0x4D36, 0x7400, 0x9D00, true);

    add_key("PageUp", 0x4900, 0x4B34, 0x7300, 0x9B00);
    add_key("PageDown", 0x5100, 0x5133, 0x7600, 0xA100);

    add_key("Equals", 0x0D3D, 0x0D2B, 0, 0x8300);

    add_key("Backtick", 0x2960, 0x297E, 0, 0);

    QString keymap = machine().settings().keymap();
    if (keymap.isEmpty())
        vlog(LogScreen, "No keymap to load!");
    else
        load_keymap(keymap);
}

u16 Screen::scan_code_from_key_event(const QKeyEvent* event) const
{
    QString key_name = key_name_from_key_event(event);

    auto modifiers = event->modifiers() & ~Qt::KeypadModifier;

    switch (modifiers) {
    case Qt::NoModifier:
        return normals[key_name];
    case Qt::ShiftModifier:
        return shifts[key_name];
    case Qt::AltModifier:
        return alts[key_name];
    case Qt::ControlModifier:
        return ctrls[key_name];
    }

    qDebug() << Q_FUNC_INFO << "Unhandled key" << event->modifiers() << key_name;
    return 0xffff;
}

static int native_key_from_key_event(const QKeyEvent* event)
{
    Q_ASSERT(event);
#if defined(Q_OS_MAC)
    return event->nativeVirtualKey();
#else
    return event->nativeScanCode();
#endif
}

QString Screen::key_name_from_key_event(const QKeyEvent* event) const
{
    switch (event->key()) {
    case Qt::Key_unknown:
        return QString();
    case Qt::Key_Alt:
        return "LAlt";
    case Qt::Key_Control:
        return "LCtrl";
    case Qt::Key_Shift:
        return "LShift";
    }

    int nativeKey = native_key_from_key_event(event);
    if (!m_key_mappings.contains(nativeKey))
        return "(unmapped)";
    return m_key_mappings[nativeKey];
}

void Screen::keyPressEvent(QKeyEvent* event)
{
    // FIXME: Respect "typematic" mode of keyboard.
    if (event->isAutoRepeat()) {
        vlog(LogScreen, "Ignoring auto-repeat KeyPress");
        return;
    }

    QString key_name = key_name_from_key_event(event);

    if (key_name.isEmpty()) {
        qDebug() << "KeyPress: Unknown key" << native_key_from_key_event(event);
        return;
    }

    if (key_name == "(unmapped)") {
        qDebug() << "KeyPress: Unmapped key" << native_key_from_key_event(event);
        return;
    }

    u16 scancode = scan_code_from_key_event(event);

    if (!machine().keyboard().is_enabled()) {
        vlog(LogScreen, "KeyPress event while keyboard disabled");
        return;
    }

    //qDebug() << "KeyPress:" << nativeKeyFromKeyEvent(event) << "mapped to" << key_name << "modifiers" << event->modifiers() << "scancode:" << scancode;

    if (key_name == "F11")
        grabMouse(QCursor(Qt::BlankCursor));
    else if (key_name == "F12")
        releaseMouse();

    QMutexLocker locker(&d->key_queue_lock);

    if (scancode != 0) {
        d->key_queue.enqueue(scancode);
        //printf("Queued %04X (%s)\n", scancode, qPrintable(key_name));
    }

    if (extended[key_name])
        d->raw_queue.enqueue(0xE0);

    d->raw_queue.enqueue(make_code[key_name]);

    machine().keyboard().did_enqueue_data();
}

void Screen::keyReleaseEvent(QKeyEvent* event)
{
    // FIXME: Respect "typematic" mode of keyboard.
    if (event->isAutoRepeat()) {
        vlog(LogScreen, "Ignoring auto-repeat KeyRelease");
        return;
    }

    if (!machine().keyboard().is_enabled()) {
        vlog(LogScreen, "KeyRelease event while keyboard disabled");
        return;
    }

    QMutexLocker l(&d->key_queue_lock);
    QString key_name = key_name_from_key_event(event);

    if (extended[key_name])
        d->raw_queue.enqueue(0xE0);

    d->raw_queue.enqueue(break_code[key_name]);
    machine().keyboard().did_enqueue_data();
    event->ignore();
}

u16 Screen::next_key()
{
    QMutexLocker l(&d->key_queue_lock);

    d->raw_queue.clear();
    if (!d->key_queue.isEmpty())
        return d->key_queue.dequeue();

    return 0;
}

u16 Screen::peek_key()
{
    QMutexLocker l(&d->key_queue_lock);

    d->raw_queue.clear();
    if (!d->key_queue.isEmpty())
        return d->key_queue.head();

    return 0;
}

u8 Screen::pop_key_data()
{
    QMutexLocker l(&d->key_queue_lock);

    u8 key = 0;
    if (!d->raw_queue.isEmpty())
        key = d->raw_queue.dequeue();
    return key;
}

bool Screen::has_raw_key()
{
    QMutexLocker l(&d->key_queue_lock);
    return !d->raw_queue.isEmpty();
}

void Screen::flush_key_buffer()
{
    QMutexLocker l(&d->key_queue_lock);

    if (!d->raw_queue.isEmpty() && machine().cpu().get_if())
        machine().keyboard().did_enqueue_data();
}

bool kbd_has_data()
{
    if (!s_self)
        return false;
    return s_self->has_raw_key();
}

u16 kbd_getc()
{
    if (!s_self)
        return 0x0000;
    return s_self->next_key();
}

u16 kbd_hit()
{
    if (!s_self)
        return 0x0000;
    return s_self->peek_key();
}

u8 kbd_pop_raw()
{
    if (!s_self)
        return 0x00;
    return s_self->pop_key_data();
}
