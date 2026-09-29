// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// docs/TESTING.md, section "Interactive desktop tests", states the conditions of the
// MARUPOP_LIVE_DESKTOP gate.
#pragma once

#include "capture/framesource.h"

#include <QByteArray>
#include <QEventLoop>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <QtEnvironmentVariables>

#include <algorithm>
#include <optional>
#include <vector>

namespace maru::test
{

inline bool liveDesktop()
{
    return qEnvironmentVariable("MARUPOP_LIVE_DESKTOP") == QLatin1String("1");
}

// The Q*Application constructor fixes the platform plugin. The live cases need the monitor handles
// and HWNDs of the windows platform plugin.
inline void selectLivePlatform()
{
    if (liveDesktop()) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("windows"));
    }
}

inline constexpr const char *liveDesktopSkip =
    "set MARUPOP_LIVE_DESKTOP=1 to run the cases that show windows on the interactive desktop, grab the "
    "screen and move the pointer";

// Qt::WindowDoesNotAcceptFocus and Qt::WA_ShowWithoutActivating keep the foreground window of the
// tester unchanged.
class LiveWindow : public QWidget
{
public:
    LiveWindow()
    {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowDoesNotAcceptFocus);
        setAttribute(Qt::WA_ShowWithoutActivating);
    }
};

// Returns nullopt on failed(), whose message goes to error, and after a 5 s timeout.
// test::waitFor() polls every 1 ms, and each poll on Windows adds up to one 15.6 ms timer period to
// a measured grab.
inline std::optional<capture::Frame>
grabFrame(capture::FrameSource &source, const QRect &rect, QString *error = nullptr)
{
    std::optional<capture::Frame> result;
    QEventLoop loop;
    const auto ready =
        QObject::connect(&source, &capture::FrameSource::frameReady, &loop, [&](const capture::Frame &frame) {
            result = frame;
            loop.quit();
        });
    const auto failed = QObject::connect(&source, &capture::FrameSource::failed, &loop, [&](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        loop.quit();
    });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    source.grab(rect);
    loop.exec();
    QObject::disconnect(ready);
    QObject::disconnect(failed);
    return result;
}

// The q quantile of values by nearest rank, 0 for an empty vector.
inline double percentile(std::vector<double> values, double q)
{
    std::ranges::sort(values);
    return values.empty() ? 0.0 : values[std::min(values.size() - 1, static_cast<size_t>(q * values.size()))];
}

} // namespace maru::test
