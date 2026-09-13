// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Paints Japanese text at a known place on the live session and warps the pointer onto it, which
// is what turns "hover over a word and watch the popup" into something a script can run.
//
// The two halves each exist because Wayland has no other answer. A plain toplevel cannot place
// itself, so the text panel is a layer surface (layer Top, anchored top-left, margins), which
// makes its logical geometry the numbers this program chose rather than whatever the compositor
// decided. And a client cannot move the pointer, so the warp goes through
// org_kde_kwin_fake_input, which KWin binds only for a process whose desktop entry declares
// X-KDE-Wayland-Interfaces=org_kde_kwin_fake_input -- tools/install-dev-desktop.sh writes that
// entry.
//
// Build: cmake -B build -DMARUPOP_BUILD_DEV_TOOLS=ON && cmake --build build --target
//        marupop-hoverprobe
// Run:   tools/install-dev-desktop.sh build
//        ./build/bin/marupop-hoverprobe --at 200,200 --hover 0:0 --hover 1:0 --hover 2:0
//
// Two motions are available. --hover holds the pointer on one character for --hover-delay, which
// is what a scan needs. --sweep moves it along a straight line, one warp of --sweep-step logical
// pixels every --sweep-interval milliseconds, defaulting to the 8 ms pump the cursor relay runs
// at; every warp prints a "track X,Y at <epoch ms>" line, so the log of the process under test
// can be read against the pointer's own track. Use this to correlate popup updates with pointer movement.
//
// Without --hover and without --sweep the panel is shown, the character rectangles are printed and
// the program waits for --seconds, which is enough to hover by hand.
#include "capture/screenlayout.h"
#include "wayland-fake-input-client-protocol.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QWindow>
#include <QtGui/qguiapplication_platform.h>

#include <LayerShellQt/Window>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <wayland-client-core.h>

namespace
{

// Default text exercises a noun phrase, compound sentence and multi-step deconjugation.
// Pixel size and colors are configurable to probe resolution and contrast.
constexpr int kFontPointSize = 28;
constexpr int kMargin = 24;
constexpr int kLineSpacing = 16;

// How one panel is painted, which is the pair the scan ladder's behaviour depends on: the extent
// a character reaches the detector input at, and the contrast the detector separates it from the
// background by.
struct PanelStyle
{
    // Glyph height in logical pixels, which overrides kFontPointSize where it is above 0.
    int pixelSize = 0;
    QColor background{0xF4, 0xF2, 0xEC};
    QColor foreground{0x1A, 0x1A, 0x1A};
};

const QStringList &defaultLines()
{
    static const QStringList lines = {
        QString::fromUtf8("今日は天気がいいですね。"),
        QString::fromUtf8("食べさせられなかった"),
        QString::fromUtf8("日本語のテキスト"),
    };
    return lines;
}

// The panel. It paints the lines itself rather than stacking QLabels, so the position of every
// character is a number this program computed and can print, not one a layout produced.
class TextPanel : public QWidget
{
public:
    TextPanel(QStringList lines, PanelStyle style)
        : m_lines(std::move(lines))
        , m_style(style)
    {
        QFont font = this->font();
        if (m_style.pixelSize > 0) {
            font.setPixelSize(m_style.pixelSize);
        } else {
            font.setPointSize(kFontPointSize);
        }
        setFont(font);
        m_metrics = std::make_unique<QFontMetricsF>(font);

        qreal width = 0;
        for (const QString &line : m_lines) {
            width = std::max(width, m_metrics->horizontalAdvance(line));
        }
        const qreal lineHeight = m_metrics->height() + kLineSpacing;
        setFixedSize(static_cast<int>(std::ceil(width)) + 2 * kMargin,
                     static_cast<int>(std::ceil(lineHeight * static_cast<qreal>(m_lines.size()))) + 2 * kMargin);
    }

    // The rectangle of one character, in this widget's coordinates. line and index are
    // zero-based; an index past the end of the line answers an empty rectangle.
    [[nodiscard]] QRectF characterRect(int line, int index) const
    {
        if (line < 0 || line >= m_lines.size()) {
            return {};
        }
        const QString &text = m_lines.at(line);
        if (index < 0 || index >= text.size()) {
            return {};
        }
        const qreal lineHeight = m_metrics->height() + kLineSpacing;
        const qreal left = kMargin + m_metrics->horizontalAdvance(text.left(index));
        const qreal right = kMargin + m_metrics->horizontalAdvance(text.left(index + 1));
        const qreal top = kMargin + lineHeight * static_cast<qreal>(line);
        return QRectF{left, top, right - left, m_metrics->height()};
    }

    [[nodiscard]] int lineCount() const
    {
        return static_cast<int>(m_lines.size());
    }

    [[nodiscard]] int lineLength(int line) const
    {
        return line >= 0 && line < m_lines.size() ? static_cast<int>(m_lines.at(line).size()) : 0;
    }

    [[nodiscard]] QString lineText(int line) const
    {
        return line >= 0 && line < m_lines.size() ? m_lines.at(line) : QString{};
    }

protected:
    void paintEvent(QPaintEvent * /*event*/) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), m_style.background);
        painter.setPen(m_style.foreground);
        painter.setFont(font());
        const qreal lineHeight = m_metrics->height() + kLineSpacing;
        for (qsizetype i = 0; i < m_lines.size(); ++i) {
            const qreal top = kMargin + lineHeight * static_cast<qreal>(i);
            painter.drawText(QPointF{static_cast<qreal>(kMargin), top + m_metrics->ascent()}, m_lines.at(i));
        }
    }

private:
    QStringList m_lines;
    PanelStyle m_style;
    std::unique_ptr<QFontMetricsF> m_metrics;
};

// The fake-input global, bound lazily on the display Qt already owns. Null when KWin refused to
// advertise it, which is what happens without the desktop entry.
class FakeInput
{
public:
    bool bind()
    {
        auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
        if (wayland == nullptr) {
            return false;
        }
        wl_display *display = wayland->display();
        if (display == nullptr) {
            return false;
        }
        // A registry of this program's own: Qt's QtWayland registry is private and binds only
        // the globals its platform plugin knows.
        wl_registry *registry = wl_display_get_registry(display);
        static const wl_registry_listener listener = {
            .global =
                [](void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
                    if (qstrcmp(interface, org_kde_kwin_fake_input_interface.name) != 0) {
                        return;
                    }
                    auto *self = static_cast<FakeInput *>(data);
                    self->m_fakeInput = static_cast<org_kde_kwin_fake_input *>(
                        wl_registry_bind(registry,
                                         name,
                                         &org_kde_kwin_fake_input_interface,
                                         std::min(version, 3U))); // 3 is pointer_motion_absolute
                },
            .global_remove = [](void *, wl_registry *, uint32_t) {},
        };
        wl_registry_add_listener(registry, &listener, this);
        wl_display_roundtrip(display);
        wl_registry_destroy(registry);
        if (m_fakeInput == nullptr) {
            return false;
        }
        org_kde_kwin_fake_input_authenticate(
            m_fakeInput, "MaruPop hover probe", "moving the pointer onto known text to exercise the scan pipeline");
        m_display = display;
        wl_display_roundtrip(display);
        return true;
    }

    // Logical global desktop coordinates, the same space QScreen::geometry() uses: KWin reads
    // pointer_motion_absolute in its own global compositor space, which is that space.
    void warp(QPointF position) const
    {
        if (m_fakeInput == nullptr) {
            return;
        }
        org_kde_kwin_fake_input_pointer_motion_absolute(
            m_fakeInput, wl_fixed_from_double(position.x()), wl_fixed_from_double(position.y()));
        wl_display_flush(m_display);
    }

    [[nodiscard]] bool isBound() const
    {
        return m_fakeInput != nullptr;
    }

private:
    wl_display *m_display = nullptr;
    org_kde_kwin_fake_input *m_fakeInput = nullptr;
};

// "line:index", or "line" for the first character of that line.
bool parseTarget(const QString &text, int *line, int *index)
{
    const qsizetype colon = text.indexOf(QLatin1Char(':'));
    bool ok = false;
    *line = (colon < 0 ? text : text.left(colon)).toInt(&ok);
    if (!ok) {
        return false;
    }
    if (colon < 0) {
        *index = 0;
        return true;
    }
    *index = text.mid(colon + 1).toInt(&ok);
    return ok;
}

QPoint parsePoint(const QString &text, QPoint fallback)
{
    const QStringList parts = text.split(QLatin1Char(','));
    if (parts.size() != 2) {
        return fallback;
    }
    bool xOk = false;
    bool yOk = false;
    const int x = parts.at(0).toInt(&xOk);
    const int y = parts.at(1).toInt(&yOk);
    return xOk && yOk ? QPoint{x, y} : fallback;
}

// "X1,Y1:X2,Y2", in the same logical global coordinates as --at and the printed character boxes.
bool parseSegment(const QString &text, QPoint *from, QPoint *to)
{
    const QStringList halves = text.split(QLatin1Char(':'));
    if (halves.size() != 2) {
        return false;
    }
    const QPoint sentinel{-1000000, -1000000};
    *from = parsePoint(halves.at(0), sentinel);
    *to = parsePoint(halves.at(1), sentinel);
    return *from != sentinel && *to != sentinel;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app{argc, argv};
    QCoreApplication::setApplicationName(QStringLiteral("marupop-hoverprobe"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Shows Japanese text at a known place and warps the pointer onto it."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("line"),
                                 QStringLiteral("A line of text to show instead of the three built-in ones."),
                                 QStringLiteral("[line…]"));
    const QCommandLineOption atOption{QStringLiteral("at"),
                                      QStringLiteral("Top-left of the panel in logical global coordinates."),
                                      QStringLiteral("X,Y"),
                                      QStringLiteral("200,200")};
    const QCommandLineOption hoverOption{
        QStringLiteral("hover"),
        QStringLiteral("Warp the pointer to the centre of one character, given as line:index (both zero-based, "
                       "index defaults to 0). Repeatable; the warps are spaced by --hover-delay."),
        QStringLiteral("line:index")};
    const QCommandLineOption delayOption{QStringLiteral("hover-delay"),
                                         QStringLiteral("Milliseconds between warps, and before the first one."),
                                         QStringLiteral("ms"),
                                         QStringLiteral("2500")};
    const QCommandLineOption secondsOption{QStringLiteral("seconds"),
                                           QStringLiteral("How long the panel stays up after the last warp."),
                                           QStringLiteral("n"),
                                           QStringLiteral("3")};
    const QCommandLineOption awayOption{
        QStringLiteral("away"),
        QStringLiteral("Warp to this point after the last --hover, which is the empty-area case the popup has to "
                       "hide for."),
        QStringLiteral("X,Y")};
    // A warp per --hover holds the pointer still, which is what a scan needs but not what a
    // hand does. The sweep moves the pointer the way the relay reports it -- one small step per
    // pump interval -- so the popup's own log lines can be read against the pointer's, and the
    // distance between the two is the lag a user sees.
    const QCommandLineOption sweepOption{
        QStringLiteral("sweep"),
        QStringLiteral("Warp the pointer along a straight line, given as X1,Y1:X2,Y2 in logical global "
                       "coordinates. Runs after the last --hover. Repeatable."),
        QStringLiteral("X1,Y1:X2,Y2")};
    const QCommandLineOption sweepStepOption{QStringLiteral("sweep-step"),
                                             QStringLiteral("Logical pixels between two warps of a sweep."),
                                             QStringLiteral("px"),
                                             QStringLiteral("4")};
    const QCommandLineOption sweepIntervalOption{
        QStringLiteral("sweep-interval"),
        QStringLiteral("Milliseconds between two warps of a sweep. The default matches the 8 ms pump the cursor "
                       "relay runs at while it is tracking."),
        QStringLiteral("ms"),
        QStringLiteral("8")};
    // Panel appearance for repeatable resolution and contrast checks.
    const QCommandLineOption pixelSizeOption{
        QStringLiteral("pixel-size"),
        QStringLiteral("Glyph height in logical pixels, instead of the 28 pt default."),
        QStringLiteral("px")};
    const QCommandLineOption foregroundOption{
        QStringLiteral("foreground"), QStringLiteral("Text colour, as a name or #RRGGBB."), QStringLiteral("colour")};
    const QCommandLineOption backgroundOption{
        QStringLiteral("background"), QStringLiteral("Panel colour, as a name or #RRGGBB."), QStringLiteral("colour")};
    parser.addOptions({atOption,
                       hoverOption,
                       delayOption,
                       secondsOption,
                       awayOption,
                       sweepOption,
                       sweepStepOption,
                       sweepIntervalOption,
                       pixelSizeOption,
                       foregroundOption,
                       backgroundOption});
    parser.process(app);

    QStringList lines = parser.positionalArguments();
    if (lines.isEmpty()) {
        lines = defaultLines();
    }

    const QPoint at = parsePoint(parser.value(atOption), QPoint{200, 200});
    // The screen holding --at, not the primary one: the anchors below are that screen's top
    // and left edge, so a panel anchored to the wrong screen lands at a negative margin and
    // the printed rectangles describe a place the text is not.
    QScreen *screen = QGuiApplication::screenAt(at);
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen == nullptr) {
        std::fprintf(stderr, "no screen\n");
        return 1;
    }

    // Each of the three is rejected rather than defaulted. A silent fallback paints a different
    // panel than the operator asked for, and the measurement taken from it names the wrong rung of
    // the ladder: QString::toInt() answers 0 for "14px", and QColor answers an opaque black for a
    // name it fails to parse.
    PanelStyle style;
    if (parser.isSet(pixelSizeOption)) {
        bool parsed = false;
        style.pixelSize = parser.value(pixelSizeOption).toInt(&parsed);
        if (!parsed || style.pixelSize < 1) {
            std::fprintf(stderr, "bad --pixel-size value: %s\n", qPrintable(parser.value(pixelSizeOption)));
            return 1;
        }
    }
    for (const auto &[option, target] :
         {std::pair{&foregroundOption, &style.foreground}, std::pair{&backgroundOption, &style.background}}) {
        if (!parser.isSet(*option)) {
            continue;
        }
        const QColor color{parser.value(*option)};
        if (!color.isValid()) {
            std::fprintf(stderr,
                         "bad --%s value: %s\n",
                         qPrintable(option->names().constFirst()),
                         qPrintable(parser.value(*option)));
            return 1;
        }
        *target = color;
    }

    TextPanel panel(lines, style);
    panel.setWindowTitle(QStringLiteral("MaruPop hover probe"));
    panel.winId(); // realizes the QWindow so the layer surface can be configured
    if (QWindow *handle = panel.windowHandle()) {
        if (auto *layer = LayerShellQt::Window::get(handle)) {
            // Top rather than Overlay: the popup itself is an overlay surface and has to draw
            // above this panel.
            layer->setLayer(LayerShellQt::Window::LayerTop);
            layer->setScope(QStringLiteral("marupop-hoverprobe"));
            layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
            layer->setExclusiveZone(-1);
            layer->setAnchors({LayerShellQt::Window::AnchorTop, LayerShellQt::Window::AnchorLeft});
            layer->setScreen(screen);
            // The anchors are the target screen's top and left edge, so the margins are
            // relative to that screen's origin.
            const QPoint origin = screen->geometry().topLeft();
            layer->setMargins(QMargins{at.x() - origin.x(), at.y() - origin.y(), 0, 0});
            layer->setDesiredSize(panel.size());
        } else {
            std::fprintf(stderr, "LayerShellQt refused the panel surface; the geometry below is a guess\n");
        }
    }
    panel.show();

    const QRect panelRect{at, panel.size()};
    std::printf("screen %s %s, panel %s\n",
                qPrintable(screen->name()),
                qPrintable(QStringLiteral("%1x%2+%3+%4")
                               .arg(screen->geometry().width())
                               .arg(screen->geometry().height())
                               .arg(screen->geometry().x())
                               .arg(screen->geometry().y())),
                qPrintable(QStringLiteral("%1x%2+%3+%4")
                               .arg(panelRect.width())
                               .arg(panelRect.height())
                               .arg(panelRect.x())
                               .arg(panelRect.y())));
    for (int line = 0; line < panel.lineCount(); ++line) {
        const QRectF first = panel.characterRect(line, 0);
        const QRectF last = panel.characterRect(line, panel.lineLength(line) - 1);
        const QRectF run = first.united(last).translated(at);
        std::printf("line %d \"%s\" run %.0f,%.0f %.0fx%.0f\n",
                    line,
                    qPrintable(panel.lineText(line)),
                    run.x(),
                    run.y(),
                    run.width(),
                    run.height());
        for (int index = 0; index < panel.lineLength(line); ++index) {
            const QRectF box = panel.characterRect(line, index).translated(at);
            std::printf("  %d:%-2d '%s' centre %.0f,%.0f box %.0f,%.0f %.0fx%.0f\n",
                        line,
                        index,
                        qPrintable(panel.lineText(line).mid(index, 1)),
                        box.center().x(),
                        box.center().y(),
                        box.x(),
                        box.y(),
                        box.width(),
                        box.height());
        }
    }
    std::fflush(stdout);

    const QStringList hovers = parser.values(hoverOption);
    const QStringList sweeps = parser.values(sweepOption);
    const int delay = parser.value(delayOption).toInt();
    const int seconds = parser.value(secondsOption).toInt();
    const int sweepStep = std::max(1, parser.value(sweepStepOption).toInt());
    const int sweepInterval = std::max(1, parser.value(sweepIntervalOption).toInt());

    FakeInput fakeInput;
    if (!hovers.isEmpty() || !sweeps.isEmpty() || parser.isSet(awayOption)) {
        if (!fakeInput.bind()) {
            std::fprintf(stderr,
                         "org_kde_kwin_fake_input is not bound: KWin advertises it only to a process whose desktop "
                         "entry declares X-KDE-Wayland-Interfaces=org_kde_kwin_fake_input. Run "
                         "tools/install-dev-desktop.sh <build-dir> first.\n");
            return 2;
        }
    }

    // The sweep phase, which runs once the discrete warps are done. Each --sweep segment is
    // walked in sweepStep-pixel increments at sweepInterval, and every warp is printed with the
    // same epoch millisecond the warps above use, so the log of the process under test lines up
    // against the pointer's own track.
    QTimer sweepTimer;
    sweepTimer.setInterval(sweepInterval);
    sweepTimer.setTimerType(Qt::PreciseTimer);
    int sweepIndex = 0;
    int sweepStepIndex = 0;
    int sweepStepCount = 0;
    QPoint sweepFrom;
    QPoint sweepTo;

    const auto beginNextSweep = [&]() -> bool {
        while (sweepIndex < sweeps.size()) {
            const QString &segment = sweeps.at(sweepIndex);
            ++sweepIndex;
            if (!parseSegment(segment, &sweepFrom, &sweepTo)) {
                std::fprintf(stderr, "bad --sweep value: %s\n", qPrintable(segment));
                continue;
            }
            const QPoint delta = sweepTo - sweepFrom;
            const double length = std::hypot(static_cast<double>(delta.x()), static_cast<double>(delta.y()));
            sweepStepCount = std::max(1, static_cast<int>(std::lround(length / sweepStep)));
            sweepStepIndex = 0;
            std::printf("sweep %d,%d -> %d,%d in %d steps of %d px every %d ms at %lld\n",
                        sweepFrom.x(),
                        sweepFrom.y(),
                        sweepTo.x(),
                        sweepTo.y(),
                        sweepStepCount,
                        sweepStep,
                        sweepInterval,
                        static_cast<long long>(QDateTime::currentMSecsSinceEpoch()));
            std::fflush(stdout);
            return true;
        }
        return false;
    };

    // One timer stepping through the warps, so the pointer sits still between them for as long
    // as a scan needs.
    int step = 0;
    QTimer timer;
    timer.setInterval(delay);

    const auto finish = [&] {
        timer.stop();
        sweepTimer.stop();
        if (parser.isSet(awayOption)) {
            const QPoint away = parsePoint(parser.value(awayOption), QPoint{});
            std::printf("warp away -> %d,%d at %lld\n",
                        away.x(),
                        away.y(),
                        static_cast<long long>(QDateTime::currentMSecsSinceEpoch()));
            std::fflush(stdout);
            fakeInput.warp(QPointF{away});
        }
        QTimer::singleShot(seconds * 1000, &app, &QCoreApplication::quit);
    };

    QObject::connect(&sweepTimer, &QTimer::timeout, &app, [&] {
        if (sweepStepIndex > sweepStepCount) {
            if (!beginNextSweep()) {
                finish();
            }
            return;
        }
        const double ratio = static_cast<double>(sweepStepIndex) / sweepStepCount;
        const QPointF point{sweepFrom.x() + ((sweepTo.x() - sweepFrom.x()) * ratio),
                            sweepFrom.y() + ((sweepTo.y() - sweepFrom.y()) * ratio)};
        std::printf("track %.0f,%.0f at %lld\n",
                    point.x(),
                    point.y(),
                    static_cast<long long>(QDateTime::currentMSecsSinceEpoch()));
        std::fflush(stdout);
        fakeInput.warp(point);
        ++sweepStepIndex;
    });

    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        if (step < hovers.size()) {
            int line = 0;
            int index = 0;
            if (!parseTarget(hovers.at(step), &line, &index)) {
                std::fprintf(stderr, "bad --hover value: %s\n", qPrintable(hovers.at(step)));
            } else {
                const QRectF box = panel.characterRect(line, index).translated(at);
                if (box.isEmpty()) {
                    std::fprintf(stderr, "no character at %s\n", qPrintable(hovers.at(step)));
                } else {
                    std::printf("warp %d:%d '%s' -> %.0f,%.0f at %lld\n",
                                line,
                                index,
                                qPrintable(panel.lineText(line).mid(index, 1)),
                                box.center().x(),
                                box.center().y(),
                                static_cast<long long>(QDateTime::currentMSecsSinceEpoch()));
                    std::fflush(stdout);
                    fakeInput.warp(box.center());
                }
            }
            ++step;
            return;
        }
        timer.stop();
        if (beginNextSweep()) {
            sweepTimer.start();
            return;
        }
        finish();
    });

    if (hovers.isEmpty() && sweeps.isEmpty() && !parser.isSet(awayOption)) {
        QTimer::singleShot(seconds * 1000, &app, &QCoreApplication::quit);
    } else if (hovers.isEmpty()) {
        // No warp to wait for, but the panel still has to be mapped and the compositor still has
        // to have configured the surface before the pointer is moved onto it.
        QTimer::singleShot(delay, &app, [&] {
            if (beginNextSweep()) {
                sweepTimer.start();
                return;
            }
            finish();
        });
    } else {
        timer.start();
    }
    return QCoreApplication::exec();
}
