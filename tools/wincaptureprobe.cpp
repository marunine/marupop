// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Times capture::WinFrameSource on the interactive desktop, for every screen, region size and
// grab path. The grab latency runs from WinFrameSource::grab() to frameReady() on the GUI thread,
// where ScanController receives frames.
#include "capture/framesource.h"
#include "capture/winframesource.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCursor>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QScreen>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace maru;

namespace
{

struct Stats
{
    double min = 0;
    double p50 = 0;
    double p90 = 0;
    double p99 = 0;
    double max = 0;
};

Stats statsOf(std::vector<double> values)
{
    Stats stats;
    if (values.empty()) {
        return stats;
    }
    std::sort(values.begin(), values.end());
    const auto at = [&](double q) {
        return values[std::min(values.size() - 1,
                               static_cast<size_t>(q * static_cast<double>(values.size() - 1) + 0.5))];
    };
    stats.min = values.front();
    stats.p50 = at(0.5);
    stats.p90 = at(0.9);
    stats.p99 = at(0.99);
    stats.max = values.back();
    return stats;
}

void print(const char *label, const Stats &stats, const char *unit)
{
    std::printf("  %-34s min %8.3f  p50 %8.3f  p90 %8.3f  p99 %8.3f  max %8.3f %s\n",
                label,
                stats.min,
                stats.p50,
                stats.p90,
                stats.p99,
                stats.max,
                unit);
}

// Returns the grab-to-delivery time in ms, or -1 after a failure or a timeout.
double timedGrab(capture::WinFrameSource &source, const QRect &rect, capture::Frame *frame)
{
    QEventLoop loop;
    double elapsed = -1;
    QElapsedTimer timer;
    const auto ready =
        QObject::connect(&source, &capture::FrameSource::frameReady, &loop, [&](const capture::Frame &f) {
            elapsed = static_cast<double>(timer.nsecsElapsed()) / 1e6;
            *frame = f;
            loop.quit();
        });
    const auto failed = QObject::connect(&source, &capture::FrameSource::failed, &loop, [&](const QString &message) {
        std::printf("  grab failed: %s\n", qPrintable(message));
        loop.quit();
    });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    timer.start();
    source.grab(rect);
    loop.exec();
    QObject::disconnect(ready);
    QObject::disconnect(failed);
    return elapsed;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Times capture::WinFrameSource on the interactive desktop."));
    parser.addHelpOption();
    const QCommandLineOption iterationsOption(QStringLiteral("iterations"),
                                              QStringLiteral("Grabs per case (default 200)."),
                                              QStringLiteral("N"),
                                              QStringLiteral("200"));
    const QCommandLineOption intervalOption(
        QStringLiteral("interval"),
        QStringLiteral("Pause between grabs in ms (default 8, the pointer polling interval)."),
        QStringLiteral("MS"),
        QStringLiteral("8"));
    const QCommandLineOption sizesOption(
        QStringLiteral("sizes"),
        QStringLiteral("Region sizes in logical pixels (default 256x160,640x400,1280x800)."),
        QStringLiteral("WxH,..."),
        QStringLiteral("256x160,640x400,1280x800"));
    const QCommandLineOption screenOption(QStringLiteral("screen"),
                                          QStringLiteral("Measure only screen INDEX (default: every screen)."),
                                          QStringLiteral("INDEX"),
                                          QStringLiteral("-1"));
    parser.addOptions({iterationsOption, intervalOption, sizesOption, screenOption});
    parser.process(app);

    const int iterations = std::max(1, parser.value(iterationsOption).toInt());
    const int interval = std::max(0, parser.value(intervalOption).toInt());
    const int onlyScreen = parser.value(screenOption).toInt();
    QList<QSize> sizes;
    for (const QString &size : parser.value(sizesOption).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QStringList parts = size.split(QLatin1Char('x'));
        if (parts.size() == 2) {
            sizes.append(QSize{parts.at(0).toInt(), parts.at(1).toInt()});
        }
    }

    capture::WinFrameSource source;
    const QList<QScreen *> screens = QGuiApplication::screens();
    std::printf("Qt %s, %lld screens, %d grabs per case, %d ms between grabs\n",
                qVersion(),
                static_cast<long long>(screens.size()),
                iterations,
                interval);

    for (qsizetype index = 0; index < screens.size(); ++index) {
        if (onlyScreen >= 0 && index != onlyScreen) {
            continue;
        }
        QScreen *screen = screens.at(index);
        const QRect native = capture::WinFrameSource::nativeGeometry(screen);
        std::printf("\nscreen %lld %s: logical %d,%d %dx%d, device %d,%d %dx%d, ratio %.2f, %.0f Hz\n",
                    static_cast<long long>(index),
                    qPrintable(screen->name()),
                    screen->geometry().x(),
                    screen->geometry().y(),
                    screen->geometry().width(),
                    screen->geometry().height(),
                    native.x(),
                    native.y(),
                    native.width(),
                    native.height(),
                    screen->devicePixelRatio(),
                    screen->refreshRate());
        for (const QSize &size : std::as_const(sizes)) {
            QRect rect{QPoint{}, size.boundedTo(screen->geometry().size())};
            rect.moveCenter(screen->geometry().center());
            for (const bool duplication : {true, false}) {
                source.setDesktopDuplicationEnabled(duplication);
                capture::Frame frame;
                // The first DXGI grab creates and seeds the duplication.
                std::vector<double> first;
                for (int i = 0; i < 3; ++i) {
                    first.push_back(timedGrab(source, rect, &frame));
                }
                std::vector<double> grabs;
                std::vector<double> hashes;
                grabs.reserve(static_cast<size_t>(iterations));
                hashes.reserve(static_cast<size_t>(iterations));
                int wrongPath = 0;
                for (int i = 0; i < iterations; ++i) {
                    const double ms = timedGrab(source, rect, &frame);
                    if (ms < 0) {
                        continue;
                    }
                    grabs.push_back(ms);
                    const bool expected =
                        source.lastMethod() == (duplication ? capture::WinFrameSource::Method::DesktopDuplication
                                                            : capture::WinFrameSource::Method::BitBlt);
                    wrongPath += expected ? 0 : 1;
                    QElapsedTimer hashTimer;
                    hashTimer.start();
                    (void)capture::FrameSource::hashImage(frame.image);
                    hashes.push_back(static_cast<double>(hashTimer.nsecsElapsed()) / 1e6);
                    if (interval > 0) {
                        QThread::msleep(static_cast<unsigned long>(interval));
                    }
                }
                const QByteArray label = QStringLiteral("%1 %2x%3 -> %4x%5")
                                             .arg(duplication ? QStringLiteral("DXGI") : QStringLiteral("BitBlt"))
                                             .arg(rect.width())
                                             .arg(rect.height())
                                             .arg(frame.image.width())
                                             .arg(frame.image.height())
                                             .toUtf8();
                print(label.constData(), statsOf(grabs), "ms");
                std::printf("    first grab %.3f ms; hash p50 %.3f ms; %d of %zu grabs answered by the other path\n",
                            first.empty() ? 0.0 : first.front(),
                            statsOf(hashes).p50,
                            wrongPath,
                            grabs.size());
            }
        }
    }

    std::vector<double> cursor;
    cursor.reserve(20000);
    for (int i = 0; i < 20000; ++i) {
        QElapsedTimer timer;
        timer.start();
        const QPoint position = QCursor::pos();
        cursor.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e3);
        Q_UNUSED(position)
    }
    std::printf("\n");
    print("QCursor::pos()", statsOf(cursor), "us");
    return 0;
}
