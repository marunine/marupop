// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// KWin cursor relay harness. Reports timestamp-based delivery latency and sample intervals
// from the script's Date.now() stamp to the receiving D-Bus slot.
//
// It runs the production bootstrap: maru::cursor::KWinScriptRelay writes the package, enables it
// in kwinrc, asks KWin to reconfigure, and falls back to loadScript(). The script is unloaded
// when the probe exits; the package and the kwinrc key are left in place, which is the state
// the application installs.
//
// The bus name io.github.marunine.marupop has to be free, so a running MaruPop has to be quit
// first: the script addresses that name and KDBusService owns it in the application.
//
// Usage: marupop-cursorprobe [SECONDS] [--idle]
//        --idle leaves tracking off, which keeps the script on its 500 ms heartbeat.
#include "cursor/cursorsink.h"
#include "cursor/kwinscriptrelay.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDateTime>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace maru::cursor;

namespace
{

double percentile(std::vector<double> values, double quantile)
{
    if (values.empty()) {
        return 0;
    }
    std::ranges::sort(values);
    const auto index = static_cast<size_t>(quantile * static_cast<double>(values.size() - 1));
    return values[std::min(index, values.size() - 1)];
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    int seconds = 15;
    bool tracking = true;
    const QStringList arguments = QCoreApplication::arguments();
    for (qsizetype i = 1; i < arguments.size(); ++i) {
        if (arguments.at(i) == QLatin1StringView("--idle")) {
            tracking = false;
        } else {
            seconds = arguments.at(i).toInt();
        }
    }

    if (!KWinScriptRelay::kwinAvailable()) {
        std::fprintf(stderr, "FAIL: org.kde.KWin is not on the session bus (not a KWin Wayland session?)\n");
        return 2;
    }
    const QString serviceName = QStringLiteral("io.github.marunine.marupop");
    if (!QDBusConnection::sessionBus().registerService(serviceName)) {
        std::fprintf(stderr, "FAIL: %s is already owned; quit the running MaruPop first\n", qPrintable(serviceName));
        return 2;
    }

    auto *relay = new KWinScriptRelay(&app);
    std::printf("install directory: %s\n", qPrintable(relay->installDir()));

    std::vector<double> arrivals;
    int positions = 0;
    QObject::connect(relay, &CursorTracker::availabilityChanged, &app, [](bool available, const QString &reason) {
        std::printf("availability: %s%s%s\n",
                    available ? "yes" : "no",
                    available ? "" : " -- ",
                    available ? "" : qPrintable(reason));
        std::fflush(stdout);
    });
    QObject::connect(
        relay, &CursorTracker::positionChanged, &app, [&arrivals, &positions](QPoint logical, QScreen *screen) {
            ++positions;
            arrivals.push_back(static_cast<double>(QDateTime::currentMSecsSinceEpoch()));
            if (positions <= 3) {
                std::printf("position %d,%d on %s\n",
                            logical.x(),
                            logical.y(),
                            screen != nullptr ? qPrintable(screen->name()) : "(no output)");
                std::fflush(stdout);
            }
        });

    relay->setTracking(tracking);
    relay->start();
    std::printf("tracking %s for %d s; move the pointer\n", tracking ? "on" : "off", seconds);

    QTimer::singleShot(seconds * 1000, &app, [&] {
        const CursorSink::LatencyStats latency = relay->sink()->latency();
        std::printf("\nUpdate calls: %llu over %d s\n", static_cast<unsigned long long>(latency.samples), seconds);
        std::printf("positions delivered: %d (%.1f Hz)\n",
                    positions,
                    static_cast<double>(positions) / static_cast<double>(seconds));
        std::printf("one-way latency ms: last %.3f  min %.3f  mean %.3f  max %.3f\n",
                    latency.lastMs,
                    latency.minMs,
                    latency.meanMs,
                    latency.maxMs);
        std::vector<double> gaps;
        gaps.reserve(arrivals.size());
        for (size_t i = 1; i < arrivals.size(); ++i) {
            gaps.push_back(arrivals[i] - arrivals[i - 1]);
        }
        if (!gaps.empty()) {
            std::printf("inter-arrival ms: p50 %.2f  p90 %.2f  max %.2f\n",
                        percentile(gaps, 0.5),
                        percentile(gaps, 0.9),
                        percentile(gaps, 1.0));
        }
        // The destructor unloads the script and leaves the package installed.
        delete relay;
        QCoreApplication::quit();
    });

    return QGuiApplication::exec();
}
