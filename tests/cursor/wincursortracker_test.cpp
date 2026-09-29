// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "cursor/wincursortracker.h"
#include "eventloop.h"
#include "livedesktop.h"

#include <QCursor>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QScreen>
#include <QSignalSpy>

#include <gtest/gtest.h>
#include <windows.h>

using namespace maru;
using namespace maru::cursor;

TEST(WinCursorTracker, reportsAvailableOnStart)
{
    WinCursorTracker tracker;
    QSignalSpy availability{&tracker, &CursorTracker::availabilityChanged};
    tracker.start();
    ASSERT_TRUE(test::waitFor([&] {
        return !availability.isEmpty();
    }));
    EXPECT_TRUE(availability.constFirst().at(0).toBool());
    // availabilityChanged() fires on a change alone, and a restart keeps the availability.
    tracker.stop();
    tracker.start();
    test::pumpFor(20);
    EXPECT_EQ(availability.size(), 1);
}

TEST(WinCursorTracker, pollsAtTheTrackingRateAndTheIdleRate)
{
    WinCursorTracker tracker;
    tracker.setTracking(true);
    tracker.start();
    const quint64 startTracking = tracker.sampleCount();
    QElapsedTimer timer;
    timer.start();
    test::pumpFor(400);
    const double trackingMs = static_cast<double>(timer.elapsed());
    const quint64 tracked = tracker.sampleCount() - startTracking;
    // The tracking interval is 8 ms, 50 polls in 400 ms. The lower bound admits the 15.6 ms
    // default timer resolution of Windows and a late first timeout.
    EXPECT_GE(tracked, static_cast<quint64>(trackingMs / 16.0)) << tracked << " polls in " << trackingMs << " ms";
    EXPECT_LE(tracked, static_cast<quint64>(trackingMs / 4.0)) << tracked << " polls in " << trackingMs << " ms";

    tracker.setTracking(false);
    const quint64 startIdle = tracker.sampleCount();
    test::pumpFor(400);
    // The idle interval is 500 ms. The 400 ms window holds one idle poll and the tracking poll that
    // setTracking(false) leaves pending.
    EXPECT_LE(tracker.sampleCount() - startIdle, 2U);
}

TEST(WinCursorTracker, stopsPollingOnStop)
{
    WinCursorTracker tracker;
    tracker.setTracking(true);
    tracker.start();
    test::pumpFor(50);
    tracker.stop();
    const quint64 stopped = tracker.sampleCount();
    test::pumpFor(100);
    EXPECT_EQ(tracker.sampleCount(), stopped);
}

TEST(WinCursorTracker, reportsTheLogicalPositionOfAMovedPointer)
{
    if (!test::liveDesktop()) {
        GTEST_SKIP() << test::liveDesktopSkip;
    }
    POINT original{};
    GetCursorPos(&original);
    QScreen *screen = QGuiApplication::primaryScreen();
    ASSERT_NE(screen, nullptr);
    // The top-left corner of the primary screen is (0, 0) in logical and in device coordinates. A
    // device point on the primary screen divided by devicePixelRatio() is the logical point.
    const QRect logical = screen->geometry();
    const qreal ratio = screen->devicePixelRatio();
    QPoint device{static_cast<int>(logical.width() * ratio / 4), static_cast<int>(logical.height() * ratio / 4)};
    // positionChanged() fires on a change alone, so the target differs from the pointer position.
    if (QPoint{original.x, original.y} == device) {
        device += QPoint{16, 16};
    }

    WinCursorTracker tracker;
    // cursor/cursortracker.h forward-declares QScreen, so moc records no metatype for the QScreen *
    // argument and a QSignalSpy stores an empty QVariant.
    QPoint last{-1, -1};
    QScreen *reported = nullptr;
    int samples = 0;
    QObject::connect(&tracker, &CursorTracker::positionChanged, &tracker, [&](QPoint logical, QScreen *on) {
        last = logical;
        reported = on;
        ++samples;
    });
    tracker.setTracking(true);
    tracker.start();
    ASSERT_TRUE(SetCursorPos(device.x(), device.y()));
    const QPoint expected{qRound(device.x() / ratio), qRound(device.y() / ratio)};
    const bool moved = test::waitFor([&] {
        return last == expected;
    });
    SetCursorPos(original.x, original.y);
    ASSERT_TRUE(moved) << "no sample reached " << expected.x() << "," << expected.y() << "; the last was " << last.x()
                       << "," << last.y() << " of " << samples;
    EXPECT_EQ(reported, screen);
}

int main(int argc, char **argv)
{
    test::selectLivePlatform();
    QGuiApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
