// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "app/winshortcuts.h"
#include "capture/framesource.h"
#include "capture/winframesource.h"
#include "cursor/cursortracker.h"
#include "cursor/wincursortracker.h"
#include "cursor/winlockwatcher.h"
#include "platform/backend.h"
#include "platform/session.h"
#include "win32/window.h"

#include <QGuiApplication>

#include <gtest/gtest.h>

using namespace maru;
using namespace maru::platform;

TEST(WinSession, detectsWindows)
{
    qunsetenv("MARUPOP_PLATFORM");
    EXPECT_EQ(redetect(), Session::Windows);
    EXPECT_EQ(sessionId(Session::Windows), QStringLiteral("windows"));
    bool recognized = false;
    EXPECT_EQ(parseSessionId(QStringLiteral("Windows"), &recognized), Session::Windows);
    EXPECT_TRUE(recognized);
    EXPECT_FALSE(capturesOwnWindows(Session::Windows));
}

TEST(WinBackend, buildsTheFourWindowsServices)
{
    Backend backend{Session::Windows};
    EXPECT_NE(qobject_cast<cursor::WinCursorTracker *>(backend.tracker()), nullptr);
    EXPECT_NE(qobject_cast<capture::WinFrameSource *>(backend.frames()), nullptr);
    EXPECT_NE(qobject_cast<cursor::WinLockWatcher *>(backend.lockWatcher()), nullptr);
    auto *shortcuts = qobject_cast<WinShortcuts *>(backend.shortcuts());
    ASSERT_NE(shortcuts, nullptr);
    EXPECT_TRUE(shortcuts->editableShortcuts());
    EXPECT_EQ(backend.capturesOwnWindows(), !win32::captureExclusionSupported());
}

TEST(WinBackend, reportsCaptureAvailable)
{
    Backend backend{Session::Windows};
    const CaptureReport report = backend.captureReport();
    EXPECT_TRUE(report.authorized);
    EXPECT_TRUE(report.remedy.isEmpty());
    ASSERT_FALSE(report.lines.isEmpty());
    EXPECT_TRUE(report.lines.constFirst().contains(QStringLiteral("Windows")));
}

TEST(WinBackend, answersUnavailableForALinuxSessionOverride)
{
    Backend backend{Session::KdePlasma};
    EXPECT_EQ(qobject_cast<cursor::WinCursorTracker *>(backend.tracker()), nullptr);
    EXPECT_NE(qobject_cast<capture::FakeFrameSource *>(backend.frames()), nullptr);
    EXPECT_FALSE(captureReportFor(Session::KdePlasma, false).authorized);
}

int main(int argc, char **argv)
{
    QGuiApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
