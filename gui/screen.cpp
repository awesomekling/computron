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
#include "Common.h"
#include "CPU.h"
#include "machine.h"
#include "debug.h"
#include "vga.h"
#include "busmouse.h"
#include "keyboard.h"
#include "settings.h"

#include <QtGui/QPainter>
#include <QtGui/QPaintEvent>
#include <QtGui/QBitmap>
#include <QtCore/QMutex>
#include <QtCore/QQueue>
#include <QtCore/QDebug>
#include <QtCore/QTimer>

struct fontcharbitmap_t {
    BYTE data[16];
};

static Screen *s_self = 0L;

class Renderer {
public:
    const Screen& screen() const { return m_screen; }
    const VGA& vga() const { return m_screen.machine().vga(); }

    virtual void synchronizeFont() = 0;
    virtual void synchronizeColors() = 0;
    virtual void render(QPainter&) = 0;
    virtual void willBecomeActive() = 0;
    virtual void willPaintSoon() = 0;

protected:
    explicit Renderer(Screen& screen) : m_screen(screen) { }

private:
    Screen& m_screen;
};

class TextRenderer final : public Renderer {
public:
    explicit TextRenderer(Screen& screen) : Renderer(screen) { }

    virtual void synchronizeFont() override;
    virtual void synchronizeColors() override;
    virtual void render(QPainter&) override;
    virtual void willBecomeActive() override;
    virtual void willPaintSoon() override { }

private:
    void putCharacter(QPainter&, int row, int column, BYTE color, BYTE character);

    int m_rows { 25 };
    int m_columns { 80 };
    int m_characterWidth { 8 };
    int m_characterHeight { 16 };

    QBitmap m_character[256];
    QBrush m_brush[16];
    QColor m_color[16];
};

class DummyRenderer final : public Renderer {
public:
    explicit DummyRenderer(Screen& screen) : Renderer(screen) { }

    virtual void synchronizeFont() override { }
    virtual void synchronizeColors() override { }
    virtual void render(QPainter&) override { }
    virtual void willBecomeActive() override { }
    virtual void willPaintSoon() override { }
};

class BufferedRenderer : public Renderer {
public:
    explicit BufferedRenderer(Screen&, int width, int height, int scale = 1);

    virtual void render(QPainter&) override;
    virtual void willBecomeActive() override;

protected:
    QImage m_buffer;
    BYTE* bufferBits() { return m_buffer.bits(); }

private:
    int m_scale { 1 };
};

class Mode04Renderer final : public BufferedRenderer {
public:
    explicit Mode04Renderer(Screen&);

    virtual void synchronizeFont() override { }
    virtual void synchronizeColors() override { }
    virtual void willPaintSoon() override;
};

Mode04Renderer::Mode04Renderer(Screen& screen)
    : BufferedRenderer(screen, 320, 200, 2)
{
    m_buffer.setColor(0, QColor(Qt::black).rgb());
    m_buffer.setColor(1, QColor(Qt::cyan).rgb());
    m_buffer.setColor(2, QColor(Qt::magenta).rgb());
    m_buffer.setColor(3, QColor(Qt::white).rgb());
}

class Mode0DRenderer final : public BufferedRenderer {
public:
    explicit Mode0DRenderer(Screen& screen) : BufferedRenderer(screen, 600, 400) { }

    virtual void synchronizeFont() override { }
    virtual void synchronizeColors() override;
    virtual void willPaintSoon() override;
};

class Mode12Renderer final : public BufferedRenderer {
public:
    explicit Mode12Renderer(Screen& screen) : BufferedRenderer(screen, 640, 480) { }

    virtual void synchronizeFont() override { }
    virtual void synchronizeColors() override;
    virtual void willPaintSoon() override;
};

class Mode13Renderer final : public BufferedRenderer {
public:
    explicit Mode13Renderer(Screen& screen) : BufferedRenderer(screen, 320, 200, 2) { }

    virtual void synchronizeFont() override { }
    virtual void synchronizeColors() override;
    virtual void willPaintSoon() override;
};

BufferedRenderer::BufferedRenderer(Screen& screen, int width, int height, int scale)
    : Renderer(screen)
    , m_buffer(width, height, QImage::Format_Indexed8)
    , m_scale(scale)
{
    m_buffer.fill(0);
}

struct Screen::Private
{
    QMutex keyQueueLock;

    QQueue<WORD> keyQueue;
    QQueue<BYTE> rawQueue;

    const BYTE *videoMemory;

    QTimer refreshTimer;
    QTimer periodicRefreshTimer;

    OwnPtr<TextRenderer> textRenderer;
    OwnPtr<Mode04Renderer> mode04Renderer;
    OwnPtr<Mode0DRenderer> mode0DRenderer;
    OwnPtr<Mode12Renderer> mode12Renderer;
    OwnPtr<Mode13Renderer> mode13Renderer;
    OwnPtr<DummyRenderer> dummyRenderer;
};

Screen::Screen(Machine& m)
    : QOpenGLWidget(nullptr),
      d(make<Private>()),
      m_machine(m)
{
    s_self = this;

    d->textRenderer = make<TextRenderer>(*this);
    d->mode04Renderer = make<Mode04Renderer>(*this);
    d->mode0DRenderer = make<Mode0DRenderer>(*this);
    d->mode12Renderer = make<Mode12Renderer>(*this);
    d->mode13Renderer = make<Mode13Renderer>(*this);
    d->dummyRenderer = make<DummyRenderer>(*this);

    init();
    d->videoMemory = machine().cpu().pointerToPhysicalMemory(PhysicalAddress(0xb8000));

    setFocusPolicy(Qt::ClickFocus);

    setMouseTracking(true);

    // This timer is kicked whenever screen memory is modified.
    d->refreshTimer.setSingleShot(true);
    d->refreshTimer.setInterval(50);
    connect(&d->refreshTimer, SIGNAL(timeout()), this, SLOT(refresh()));

    // This timer does a forced refresh() every second, in case we miss anything.
    // FIXME: This would not be needed if we had perfect invalidation + scanline timing.
    d->periodicRefreshTimer.setInterval(1000);
    d->periodicRefreshTimer.start();
    connect(&d->periodicRefreshTimer, SIGNAL(timeout()), this, SLOT(refresh()));

#if 0
    // HACK 2000: Type w<ENTER> at boot for Windows ;-)
    d->keyQueue.enqueue(0x1177);
    d->keyQueue.enqueue(0x1C0D);
#endif
}

Screen::~Screen()
{
}

void Screen::scheduleRefresh()
{
    if (!d->refreshTimer.isActive())
        d->refreshTimer.start();
}

void Screen::notify()
{
    if (d->refreshTimer.isActive())
        return;
    QMetaObject::invokeMethod(this, "scheduleRefresh", Qt::QueuedConnection);
}

void TextRenderer::putCharacter(QPainter& p, int row, int column, BYTE color, BYTE character)
{
    int x = column * m_characterWidth;
    int y = row * m_characterHeight;

    p.setBackground(m_brush[color >> 4]);
    p.eraseRect(x, y, m_characterWidth, m_characterHeight);
    p.setPen(m_color[color & 0xf]);
    p.drawPixmap(x, y, m_character[character]);
}

class RefreshGuard {
public:
    RefreshGuard(Machine& machine) : m_machine(machine) { m_machine.vga().willRefreshScreen(); }
    ~RefreshGuard() { m_machine.vga().didRefreshScreen(); }

private:
    Machine& m_machine;
};

inline bool isVideoModeUsingVGAMemory(BYTE videoMode)
{
    return videoMode == 0x0D || videoMode == 0x12 || videoMode == 0x13;
}

void Screen::refresh()
{
    RefreshGuard guard(machine());

    BYTE videoMode = currentVideoMode();
    bool videoModeChanged = false;

    if (m_videoModeInLastRefresh != videoMode) {
        vlog(LogScreen, "Video mode changed to %02X", videoMode);
        m_videoModeInLastRefresh = videoMode;
        videoModeChanged = true;
    }

    if (videoModeChanged) {
        renderer().willBecomeActive();
    }

    if (isVideoModeUsingVGAMemory(videoMode)) {
        if (machine().vga().isPaletteDirty()) {
            renderer().synchronizeColors();
            // FIXME: This will probably race with VGAMemory's internal palette.
            machine().vga().setPaletteDirty(false);
        }
    }

    renderer().synchronizeFont();
    renderer().synchronizeColors();
    renderer().willPaintSoon();

    update();
}

Renderer& Screen::renderer()
{
    switch (currentVideoMode()) {
    case 0x03:
        return *d->textRenderer;
    case 0x04:
        return *d->mode04Renderer;
    case 0x0D:
        return *d->mode0DRenderer;
    case 0x12:
        return *d->mode12Renderer;
    case 0x13:
        return *d->mode13Renderer;
    default:
        return *d->dummyRenderer;
    }
}

void Screen::setScreenSize(int width, int height)
{
    if (m_width == width && m_height == height)
        return;

    m_width = width;
    m_height = height;

    setFixedSize(m_width, m_height);
}

void Mode04Renderer::willPaintSoon()
{
    const BYTE* videoMemory = screen().machine().cpu().pointerToPhysicalMemory(PhysicalAddress(0xb8000));
    WORD startAddress = vga().startAddress();
    videoMemory += startAddress;
    for (unsigned scanLine = 0; scanLine < 200; ++scanLine) {
        BYTE* out = m_buffer.scanLine(scanLine);
        const BYTE* in = videoMemory;
        if ((scanLine & 1))
            in += 0x2000;
        in += (scanLine / 2) * 80;
        for (unsigned i = 0; i < 80; ++i) {
            *(out++) = (in[i] >> 6) & 3;
            *(out++) = (in[i] >> 4) & 3;
            *(out++) = (in[i] >> 2) & 3;
            *(out++) = (in[i] >> 0) & 3;
        }
    }
}

void Mode12Renderer::willPaintSoon()
{
    const BYTE *p0 = vga().plane(0);
    const BYTE *p1 = vga().plane(1);
    const BYTE *p2 = vga().plane(2);
    const BYTE *p3 = vga().plane(3);

    int offset = 0;

    BYTE* bits = bufferBits();
    for (int y = 0; y < 480; ++y) {
        BYTE* px = &bits[y*640];

        for (int x = 0; x < 640; x += 8, ++offset) {
#define D(i) ((p0[offset]>>i) & 1) | (((p1[offset]>>i) & 1)<<1) | (((p2[offset]>>i) & 1)<<2) | (((p3[offset]>>i) & 1)<<3)
            *(px++) = D(7);
            *(px++) = D(6);
            *(px++) = D(5);
            *(px++) = D(4);
            *(px++) = D(3);
            *(px++) = D(2);
            *(px++) = D(1);
            *(px++) = D(0);
        }
    }
}

void Mode0DRenderer::willPaintSoon()
{
    const BYTE *p0 = vga().plane(0);
    const BYTE *p1 = vga().plane(1);
    const BYTE *p2 = vga().plane(2);
    const BYTE *p3 = vga().plane(3);

    WORD startAddress = vga().startAddress();
    p0 += startAddress;
    p1 += startAddress;
    p2 += startAddress;
    p3 += startAddress;

    BYTE* bits = bufferBits();
    int offset = 0;

    for (int y = 0; y < 200; ++y) {
        BYTE* px = &bits[y*320];
#define A0D(i) ((p0[offset]>>i) & 1) | (((p1[offset]>>i) & 1)<<1) | (((p2[offset]>>i) & 1)<<2) | (((p3[offset]>>i) & 1)<<3)
        for (int x = 0; x < 320; x += 8, ++offset) {
            *(px++) = D(7);
            *(px++) = D(6);
            *(px++) = D(5);
            *(px++) = D(4);
            *(px++) = D(3);
            *(px++) = D(2);
            *(px++) = D(1);
            *(px++) = D(0);
        }
    }
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
    renderer().render(p);
}

void BufferedRenderer::willBecomeActive()
{
    const_cast<Screen&>(screen()).setScreenSize(m_buffer.width() * m_scale, m_buffer.height() * m_scale);
}

void BufferedRenderer::render(QPainter& p)
{
    p.drawImage(QRect(0, 0, m_buffer.width() * m_scale, m_buffer.height() * m_scale), m_buffer);
}

void Mode0DRenderer::synchronizeColors()
{
    for (unsigned i = 0; i < 16; ++i)
        m_buffer.setColor(i, vga().paletteColor(i).rgb());
}

void Mode12Renderer::synchronizeColors()
{
    for (unsigned i = 0; i < 16; ++i)
        m_buffer.setColor(i, vga().paletteColor(i).rgb());
}

void Mode13Renderer::synchronizeColors()
{
    for (unsigned i = 0; i < 256; ++i)
        m_buffer.setColor(i, vga().color(i).rgb());
}

void Mode13Renderer::willPaintSoon()
{
    const BYTE* videoMemory = vga().plane(0) + vga().startAddress();

    ValueSize mode;
    DWORD lineOffset = vga().readRegister(0x13);

    if (vga().readRegister(0x14) & 0x40) {
        mode = DWordSize;
        lineOffset <<= 3;
    } else if (vga().readRegister(0x17) & 0x40) {
        mode = ByteSize;
        lineOffset <<= 1;
    } else {
        mode = WordSize;
        lineOffset <<= 2;
    }

    auto* bits = bufferBits();
    auto* bit = bits;

    if (mode == ByteSize) {
        for (unsigned y = 0; y < 200; ++y) {
            for (unsigned x = 0; x < 320; ++x) {
                BYTE plane = x % 4;
                DWORD byteOffset = (plane * 65536) + (y * lineOffset) + (x >> 2);
                *(bit++) = videoMemory[byteOffset];
            }
        }
    } else if (mode == WordSize) {
        for (unsigned y = 0; y < 200; ++y) {
            for (unsigned x = 0; x < 320; ++x) {
                BYTE plane = x % 4;
                DWORD byteOffset = (plane * 65536) + (y * lineOffset) + ((x >> 1) & ~1);
                *(bit++) = videoMemory[byteOffset];
            }
        }
    } else if (mode == DWordSize) {
        for (unsigned y = 0; y < 200; ++y) {
            for (unsigned x = 0; x < 320; ++x) {
                BYTE plane = x % 4;
                DWORD byteOffset = (plane * 65536) + (y * lineOffset) + (x & ~3);
                *(bit++) = videoMemory[byteOffset];
            }
        }
    }
}

void TextRenderer::willBecomeActive()
{
    const_cast<Screen&>(screen()).setScreenSize(m_characterWidth * m_columns, m_characterHeight * m_rows);
}

void TextRenderer::render(QPainter& p)
{
    auto* textPtr = screen().machine().cpu().pointerToPhysicalMemory(PhysicalAddress(0xb8000));
    textPtr += vga().startAddress() * 2;

    int screenColumns = screen().currentColumnCount();

    WORD rawCursor = vga().readRegister(0x0e) << 8 | vga().readRegister(0x0f);
    BYTE row = screenColumns ? (rawCursor / screenColumns) : 0;
    BYTE column = screenColumns ? (rawCursor % screenColumns) : 0;

    // Repaint everything
    for (int y = 0; y < m_rows; ++y) {
        for (int x = 0; x < m_columns; ++x) {
            putCharacter(p, y, x, textPtr[1], textPtr[0]);
            textPtr += 2;
        }
    }

    BYTE cursorStart = vga().readRegister(0x0a);
    BYTE cursorEnd = vga().readRegister(0x0b);

    // HACK 2000!
    if (cursorEnd < 14)
    {
        cursorEnd *= 2;
        cursorStart *= 2;
    }

    //vlog(LogScreen, "rows: %d, row: %d, col: %d", m_rows, cursor.row, cursor.column);
    //vlog(LogScreen, "cursor: %d to %d", cursorStart, cursorEnd);

    //p.setCompositionMode(QPainter::CompositionMode_Xor);
    p.fillRect(column * m_characterWidth, row * m_characterHeight + cursorStart, m_characterWidth, cursorEnd - cursorStart, m_brush[14]);
}

void TextRenderer::synchronizeColors()
{
    for (int i = 0; i < 16; ++i) {
        m_color[i] = vga().paletteColor(i);
        m_brush[i] = QBrush(m_color[i]);
    }
}

void TextRenderer::synchronizeFont()
{
    auto vector = screen().machine().cpu().getRealModeInterruptVector(0x43);
    auto physicalAddress = PhysicalAddress::fromRealMode(vector);
    auto* fbmp = (const fontcharbitmap_t *)(screen().machine().cpu().pointerToPhysicalMemory(physicalAddress));

    for (int i = 0; i < 256; ++i)
        m_character[i] = QBitmap::fromData(QSize(m_characterWidth, m_characterHeight), fbmp[i].data, QImage::Format_Mono);
}

BYTE Screen::currentVideoMode() const
{
    return machine().vga().currentVideoMode();
}

BYTE Screen::currentRowCount() const
{
    // FIXME: Don't get through BDA.
    return machine().cpu().readPhysicalMemory<BYTE>(PhysicalAddress(0x484)) + 1;
}

BYTE Screen::currentColumnCount() const
{
    // FIXME: Don't get through BDA.
    return machine().cpu().readPhysicalMemory<BYTE>(PhysicalAddress(0x44a));
}

void Screen::mouseMoveEvent(QMouseEvent* e)
{
    QOpenGLWidget::mouseMoveEvent(e);
    machine().busMouse().moveEvent(e->x(), e->y());
}

void Screen::mousePressEvent(QMouseEvent* e)
{
    QOpenGLWidget::mousePressEvent(e);
    switch (e->button()) {
    case Qt::LeftButton:
        machine().busMouse().buttonPressEvent(e->x(), e->y(), BusMouse::LeftButton);
        break;
    case Qt::RightButton:
        machine().busMouse().buttonPressEvent(e->x(), e->y(), BusMouse::RightButton);
        break;
    default:
        break;
    }
}

void Screen::mouseReleaseEvent(QMouseEvent *e)
{
    QOpenGLWidget::mouseReleaseEvent(e);
    switch (e->button()) {
    case Qt::LeftButton:
        machine().busMouse().buttonReleaseEvent(e->x(), e->y(), BusMouse::LeftButton);
        break;
    case Qt::RightButton:
        machine().busMouse().buttonReleaseEvent(e->x(), e->y(), BusMouse::RightButton);
        break;
    default:
        break;
    }
}

// This sucks, any suggestions?

#include "screen.h"
#include <QHash>
#include <QKeyEvent>
#include <QDebug>
#include <QMutexLocker>

static QHash<QString, WORD> normals;
static QHash<QString, WORD> shifts;
static QHash<QString, WORD> ctrls;
static QHash<QString, WORD> alts;
static QHash<QString, BYTE> makeCode;
static QHash<QString, BYTE> breakCode;
static QHash<QString, bool> extended;

static void addKey(const QString& keyName, WORD normal, WORD shift, WORD ctrl, WORD alt, bool isExtended = false)
{
    normals.insert(keyName, normal);
    shifts.insert(keyName, shift);
    ctrls.insert(keyName, ctrl);
    alts.insert(keyName, alt);
    extended.insert(keyName, isExtended);
    makeCode.insert(keyName, (normal & 0xFF00) >> 8);
    breakCode.insert(keyName, ((normal & 0xFF00) >> 8) | 0x80);
}

bool Screen::loadKeymap(const QString& filename)
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
        BYTE nativeKey;
        if (pieces[1].startsWith("0x"))
            nativeKey = pieces[1].toUInt(&ok, 16);
        else
            nativeKey = pieces[1].toUInt(&ok);
        if (!ok) {
            printf("Invalid keymap line: '%s'\n", rawLine.data());
            continue;
        }

        //printf("Pieces: %02X => '%s'\n", nativeKey, qPrintable(pieces[0]));

        // FIXME: Check that the key name is valid.
        m_keyMappings[nativeKey] = pieces[0];
    }

    makeCode["LShift"] = 0x2A;
    makeCode["LCtrl"] = 0x1D;
    makeCode["LAlt"] = 0x38;
    makeCode["RShift"] = 0x36;
    makeCode["RCtrl"] = 0x1D;
    makeCode["RAlt"] = 0x38;

    breakCode["LShift"] = 0xAA;
    breakCode["LCtrl"] = 0x9D;
    breakCode["LAlt"] = 0xB8;

    breakCode["RShift"] = 0xB6;
    breakCode["RCtrl"] = 0x9D;
    breakCode["RAlt"] = 0xB8;

    addKey("A", 0x1E61, 0x1E41, 0x1E01, 0x1E00);
    addKey("B", 0x3062, 0x3042, 0x3002, 0x3000);
    addKey("C", 0x2E63, 0x2E43, 0x2E03, 0x2E00);
    addKey("D", 0x2064, 0x2044, 0x2004, 0x2000);
    addKey("E", 0x1265, 0x1245, 0x1205, 0x1200);
    addKey("F", 0x2166, 0x2146, 0x2106, 0x2100);
    addKey("G", 0x2267, 0x2247, 0x2207, 0x2200);
    addKey("H", 0x2368, 0x2348, 0x2308, 0x2300);
    addKey("I", 0x1769, 0x1749, 0x1709, 0x1700);
    addKey("J", 0x246A, 0x244A, 0x240A, 0x2400);
    addKey("K", 0x256B, 0x254B, 0x250B, 0x2500);
    addKey("L", 0x266C, 0x264C, 0x260C, 0x2600);
    addKey("M", 0x326D, 0x324D, 0x320D, 0x3200);
    addKey("N", 0x316E, 0x314E, 0x310E, 0x3100);
    addKey("O", 0x186F, 0x184F, 0x180F, 0x1800);
    addKey("P", 0x1970, 0x1950, 0x1910, 0x1900);
    addKey("Q", 0x1071, 0x1051, 0x1011, 0x1000);
    addKey("R", 0x1372, 0x1352, 0x1312, 0x1300);
    addKey("S", 0x1F73, 0x1F53, 0x1F13, 0x1F00);
    addKey("T", 0x1474, 0x1454, 0x1414, 0x1400);
    addKey("U", 0x1675, 0x1655, 0x1615, 0x1600);
    addKey("V", 0x2F76, 0x2F56, 0x2F16, 0x2F00);
    addKey("W", 0x1177, 0x1157, 0x1117, 0x1100);
    addKey("X", 0x2D78, 0x2D58, 0x2D18, 0x2D00);
    addKey("Y", 0x1579, 0x1559, 0x1519, 0x1500);
    addKey("Z", 0x2C7A, 0x2C5A, 0x2C1A, 0x2C00);

    addKey("1", 0x0231, 0x0221, 0,      0x7800);
    addKey("2", 0x0332, 0x0340, 0x0300, 0x7900);
    addKey("3", 0x0433, 0x0423, 0,      0x7A00);
    addKey("4", 0x0534, 0x0524, 0,      0x7B00);
    addKey("5", 0x0635, 0x0625, 0,      0x7C00);
    addKey("6", 0x0736, 0x075E, 0x071E, 0x7D00);
    addKey("7", 0x0837, 0x0826, 0,      0x7E00);
    addKey("8", 0x0938, 0x092A, 0,      0x7F00);
    addKey("9", 0x0A39, 0x0a28, 0,      0x8000);
    addKey("0", 0x0B30, 0x0B29, 0,      0x8100);

    addKey("F1", 0x3B00, 0x5400, 0x5E00, 0x6800);
    addKey("F2", 0x3C00, 0x5500, 0x5F00, 0x6900);
    addKey("F3", 0x3D00, 0x5600, 0x6000, 0x6A00);
    addKey("F4", 0x3E00, 0x5700, 0x6100, 0x6B00);
    addKey("F5", 0x3F00, 0x5800, 0x6200, 0x6C00);
    addKey("F6", 0x4000, 0x5900, 0x6300, 0x6D00);
    addKey("F7", 0x4100, 0x5A00, 0x6400, 0x6E00);
    addKey("F8", 0x4200, 0x5B00, 0x6500, 0x6F00);
    addKey("F9", 0x4300, 0x5C00, 0x6600, 0x7000);
    addKey("F10", 0x4400, 0x5D00, 0x6700, 0x7100);
    addKey("F11", 0x8500, 0x8700, 0x8900, 0x8B00);
    addKey("F12", 0x8600, 0x8800, 0x8A00, 0x8C00);

    addKey("Slash", 0x352F, 0x353F, 0, 0);
    addKey("Minus", 0x0C2D, 0x0C5F, 0xC1F, 0x8200);
    addKey("Period", 0x342E, 0x343E, 0, 0);
    addKey("Comma", 0x332C, 0x333C, 0, 0);
    addKey("Semicolon", 0x273B, 0x273A, 0, 0x2700);

    addKey("LeftBracket", 0x1A5B, 0x1A7B, 0x1A1B, 0x1A00);
    addKey("RightBracket", 0x1B5D, 0x1B7D, 0x1B1D, 0x1B00);
    addKey("Apostrophe", 0x2827, 0x2822, 0, 0);
    addKey("Backslash", 0x2B5C, 0x2B7C, 0x2B1C, 0x2600);

    addKey("Tab", 0x0F09, 0x0F00, 0x9400, 0xA500);
    addKey("Backspace", 0x0E08, 0x0E08, 0x0E7F, 0x0E00);
    addKey("Return", 0x1C0D, 0x1C0D, 0x1C0A, 0xA600);
    addKey("Space", 0x3920, 0x3920, 0x3920, 0x3920);
    addKey("Escape", 0x011B, 0x011B, 0x011B, 0x0100);

    addKey("Up", 0x4800, 0x4838, 0x8D00, 0x9800, true);
    addKey("Down", 0x5000, 0x5032, 0x9100, 0xA000, true);
    addKey("Left", 0x4B00, 0x4B34, 0x7300, 0x9B00, true);
    addKey("Right", 0x4D00, 0x4D36, 0x7400, 0x9D00, true);

    addKey("PageUp", 0x4900, 0x4B34, 0x7300, 0x9B00);
    addKey("PageDown", 0x5100, 0x5133, 0x7600, 0xA100);

    addKey("Equals", 0x0D3D, 0x0D2B, 0, 0x8300);

    addKey("Backtick", 0x2960, 0x297E, 0, 0);

    return true;
}

void Screen::init()
{
    QString keymap = machine().settings().keymap();
    if (keymap.isEmpty())
        vlog(LogScreen, "No keymap to load!");
    else
        loadKeymap(keymap);
}

WORD Screen::scanCodeFromKeyEvent(const QKeyEvent* event) const
{
    QString keyName = keyNameFromKeyEvent(event);

    auto modifiers = event->modifiers() & ~Qt::KeypadModifier;

    switch (modifiers) {
    case Qt::NoModifier:
        return normals[keyName];
    case Qt::ShiftModifier:
        return shifts[keyName];
    case Qt::AltModifier:
        return alts[keyName];
    case Qt::ControlModifier:
        return ctrls[keyName];
    }

    qDebug() << Q_FUNC_INFO << "Unhandled key" << event->modifiers() << keyName;
    return 0xffff;
}

static int nativeKeyFromKeyEvent(const QKeyEvent* event)
{
    Q_ASSERT(event);
#if defined(Q_OS_MAC)
    return event->nativeVirtualKey();
#else
    return event->nativeScanCode();
#endif
}

QString Screen::keyNameFromKeyEvent(const QKeyEvent* event) const
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

    int nativeKey = nativeKeyFromKeyEvent(event);
    if (!m_keyMappings.contains(nativeKey))
        return "(unmapped)";
    return m_keyMappings[nativeKey];
}

void Screen::keyPressEvent(QKeyEvent* event)
{
    // FIXME: Respect "typematic" mode of keyboard.
    if (event->isAutoRepeat()) {
        vlog(LogScreen, "Ignoring auto-repeat KeyPress");
        return;
    }

    QString keyName = keyNameFromKeyEvent(event);

    if (keyName.isEmpty()) {
        qDebug() << "KeyPress: Unknown key" << nativeKeyFromKeyEvent(event);
        return;
    }

    if (keyName == "(unmapped)") {
        qDebug() << "KeyPress: Unmapped key" << nativeKeyFromKeyEvent(event);
        return;
    }

    WORD scancode = scanCodeFromKeyEvent(event);

    if (!machine().keyboard().isEnabled()) {
        vlog(LogScreen, "KeyPress event while keyboard disabled");
        return;
    }

    //qDebug() << "KeyPress:" << nativeKeyFromKeyEvent(event) << "mapped to" << keyName << "modifiers" << event->modifiers() << "scancode:" << scancode;

    if (keyName == "F11")
        grabMouse(QCursor(Qt::BlankCursor));
    else if (keyName == "F12")
        releaseMouse();

    QMutexLocker locker(&d->keyQueueLock);

    if (scancode != 0) {
        d->keyQueue.enqueue(scancode);
        //printf("Queued %04X (%s)\n", scancode, qPrintable(keyName));
    }

    if (extended[keyName])
        d->rawQueue.enqueue(0xE0);

    d->rawQueue.enqueue(makeCode[keyName]);

    machine().keyboard().didEnqueueData();
}

void Screen::keyReleaseEvent(QKeyEvent* event)
{
    // FIXME: Respect "typematic" mode of keyboard.
    if (event->isAutoRepeat()) {
        vlog(LogScreen, "Ignoring auto-repeat KeyRelease");
        return;
    }

    if (!machine().keyboard().isEnabled()) {
        vlog(LogScreen, "KeyRelease event while keyboard disabled");
        return;
    }

    QMutexLocker l(&d->keyQueueLock);
    QString keyName = keyNameFromKeyEvent(event);

    if (extended[keyName])
        d->rawQueue.enqueue(0xE0);

    d->rawQueue.enqueue(breakCode[keyName]);
    machine().keyboard().didEnqueueData();
    event->ignore();
}

WORD Screen::nextKey()
{
    QMutexLocker l(&d->keyQueueLock);

    d->rawQueue.clear();
    if (!d->keyQueue.isEmpty())
        return d->keyQueue.dequeue();

    return 0;
}

WORD Screen::peekKey()
{
    QMutexLocker l(&d->keyQueueLock);

    d->rawQueue.clear();
    if (!d->keyQueue.isEmpty())
        return d->keyQueue.head();

    return 0;
}

BYTE Screen::popKeyData()
{
    QMutexLocker l(&d->keyQueueLock);

    BYTE key = 0;
    if (!d->rawQueue.isEmpty())
        key = d->rawQueue.dequeue();
    return key;
}

bool Screen::hasRawKey()
{
    QMutexLocker l(&d->keyQueueLock);
    return !d->rawQueue.isEmpty();
}

void Screen::flushKeyBuffer()
{
    QMutexLocker l(&d->keyQueueLock);

    if (!d->rawQueue.isEmpty() && machine().cpu().getIF())
        machine().keyboard().didEnqueueData();
}

bool kbd_has_data()
{
    if (!s_self)
        return false;
    return s_self->hasRawKey();
}

WORD kbd_getc()
{
    if (!s_self)
        return 0x0000;
    return s_self->nextKey();
}

WORD kbd_hit()
{
    if (!s_self)
        return 0x0000;
    return s_self->peekKey();
}

BYTE kbd_pop_raw()
{
    if (!s_self)
        return 0x00;
    return s_self->popKeyData();
}
