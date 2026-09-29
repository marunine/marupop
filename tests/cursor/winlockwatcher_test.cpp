// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// A session that LockWorkStation() locks stays locked until a user unlocks it.
#include "cursor/winlockwatcher.h"

#include <QCoreApplication>
#include <QSignalSpy>

#include <gtest/gtest.h>
#include <windows.h>
#include <wtsapi32.h>

using namespace maru::cursor;

TEST(WinLockWatcher, readsAnUnlockedSessionAtConstruction)
{
    // ctest runs in an unlocked interactive session or in a service session.
    // WTSQuerySessionInformation() reports both as unlocked.
    WinLockWatcher watcher;
    EXPECT_FALSE(watcher.isLocked());
}

TEST(WinLockWatcher, followsTheLockAndUnlockNotifications)
{
    WinLockWatcher watcher;
    QSignalSpy changed{&watcher, &LockWatcher::lockedChanged};

    watcher.handleSessionChange(WTS_SESSION_LOCK);
    EXPECT_TRUE(watcher.isLocked());
    watcher.handleSessionChange(WTS_SESSION_LOCK);
    watcher.handleSessionChange(WTS_SESSION_UNLOCK);
    EXPECT_FALSE(watcher.isLocked());

    ASSERT_EQ(changed.size(), 2);
    EXPECT_TRUE(changed.at(0).at(0).toBool());
    EXPECT_FALSE(changed.at(1).at(0).toBool());
}

TEST(WinLockWatcher, ignoresTheOtherSessionChanges)
{
    WinLockWatcher watcher;
    QSignalSpy changed{&watcher, &LockWatcher::lockedChanged};
    for (const unsigned code : {WTS_CONSOLE_CONNECT, WTS_CONSOLE_DISCONNECT, WTS_REMOTE_CONNECT, WTS_SESSION_LOGON}) {
        watcher.handleSessionChange(code);
    }
    EXPECT_TRUE(changed.isEmpty());
    EXPECT_FALSE(watcher.isLocked());
}

int main(int argc, char **argv)
{
    QCoreApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
