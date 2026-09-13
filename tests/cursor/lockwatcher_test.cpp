// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The session lock watcher queries org.freedesktop.ScreenSaver at /ScreenSaver.
// The fake owns that service on the private bus supplied by dbus-run-session, so locked,
// unlocked and unavailable-service states can be exercised without touching the host session.
#include "cursor/screensaverlockwatcher.h"
#include "eventloop.h"
#include "fakescreensaver.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QEventLoop>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTimer>

#include <gtest/gtest.h>
#include <optional>

using namespace maru::cursor;

namespace
{

// The service the watcher would reach, preferring the freedesktop name it queries first, or an
// empty string where the bus carries neither.
QString screenSaverOnBus()
{
    const auto *interface = QDBusConnection::sessionBus().interface();
    if (interface == nullptr) {
        return {};
    }
    for (const QString &service :
         {QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("org.kde.screensaver")}) {
        if (interface->isServiceRegistered(service)) {
            return service;
        }
    }
    return {};
}

// GetActive read straight off the bus, which is what the watcher has to agree with. The session
// running a suite may be locked or unlocked, so the expected value is read rather than assumed.
std::optional<bool> screenSaverActive(const QString &service)
{
    QDBusInterface screenSaver(service,
                               QStringLiteral("/ScreenSaver"),
                               QStringLiteral("org.freedesktop.ScreenSaver"),
                               QDBusConnection::sessionBus());
    const QDBusReply<bool> reply = screenSaver.call(QStringLiteral("GetActive"));
    if (!reply.isValid()) {
        return std::nullopt;
    }
    return reply.value();
}

} // namespace

TEST(ScreenSaverLockWatcherTest, reportsUnlockedBeforeTheFirstReplyArrives)
{
    ScreenSaverLockWatcher watcher;
    // The constructor's query is asynchronous, so the documented starting state holds whatever
    // the session is doing: a locked session moves the watcher only once its reply lands.
    EXPECT_FALSE(watcher.isLocked());
}

TEST(ScreenSaverLockWatcherTest, staysUnlockedWhereNoServiceAnswers)
{
    if (!screenSaverOnBus().isEmpty()) {
        GTEST_SKIP() << "a screen saver service is on this bus, so the ambient lock state decides the answer";
    }
    ScreenSaverLockWatcher watcher;
    // The two errors that follow where no service answers.
    maru::test::pumpFor(300);
    EXPECT_FALSE(watcher.isLocked());
}

TEST(ScreenSaverLockWatcherTest, readsTheCurrentStateFromTheRunningScreenSaver)
{
    if (!QDBusConnection::sessionBus().isConnected()) {
        GTEST_SKIP() << "no session bus";
    }
    const QString service = screenSaverOnBus();
    if (service.isEmpty()) {
        GTEST_SKIP() << "neither org.freedesktop.ScreenSaver nor org.kde.screensaver is on the bus";
    }
    const std::optional<bool> expected = screenSaverActive(service);
    if (!expected.has_value()) {
        GTEST_SKIP() << "GetActive answered with an error on " << qPrintable(service);
    }

    ScreenSaverLockWatcher watcher;
    QSignalSpy spy{&watcher, &LockWatcher::lockedChanged};
    maru::test::pumpFor(500);
    EXPECT_EQ(watcher.isLocked(), *expected);
    // The watcher starts unlocked, so it announces a locked session once and an unlocked one
    // never.
    EXPECT_EQ(spy.count(), *expected ? 1 : 0);

    // A second query answers the same way and emits nothing, because the value did not change.
    const int announcements = spy.count();
    watcher.query();
    maru::test::pumpFor(300);
    EXPECT_EQ(watcher.isLocked(), *expected);
    EXPECT_EQ(spy.count(), announcements);
}

namespace
{

// A fixture per case, because each one registers org.freedesktop.ScreenSaver and releases it
// again, and a GTest binary runs its cases in one process on one bus.
class LockWatcherFakeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        screenSaver = std::make_unique<maru::test::FakeScreenSaver>();
        if (!screenSaver->isRegistered()) {
            GTEST_SKIP() << screenSaver->skipReason().toStdString();
        }
    }

    void TearDown() override
    {
        screenSaver.reset();
    }

    std::unique_ptr<maru::test::FakeScreenSaver> screenSaver;
};

} // namespace

TEST_F(LockWatcherFakeTest, readsALockedSessionFromTheFirstReply)
{
    screenSaver->setActive(true);

    ScreenSaverLockWatcher watcher;
    QSignalSpy spy{&watcher, &LockWatcher::lockedChanged};
    // The constructor's GetActive is asynchronous, so the watcher reports unlocked until the
    // reply lands, which is the state that keeps scanning working on a bus that answers
    // nothing.
    EXPECT_FALSE(watcher.isLocked());

    ASSERT_TRUE(spy.wait(2000));
    EXPECT_TRUE(watcher.isLocked());
    EXPECT_EQ(spy.count(), 1);
    EXPECT_TRUE(spy.first().at(0).toBool());
}

TEST_F(LockWatcherFakeTest, readsAnUnlockedSessionWithoutAnnouncingAChange)
{
    screenSaver->setActive(false);

    ScreenSaverLockWatcher watcher;
    QSignalSpy spy{&watcher, &LockWatcher::lockedChanged};
    maru::test::pumpFor(500);

    EXPECT_FALSE(watcher.isLocked());
    // The watcher starts unlocked, so an unlocked reply changes nothing and announces nothing.
    EXPECT_EQ(spy.count(), 0);
    EXPECT_GE(screenSaver->getActiveCount(), 1);
}

TEST_F(LockWatcherFakeTest, followsEveryActiveChangedTransition)
{
    ScreenSaverLockWatcher watcher;
    QSignalSpy spy{&watcher, &LockWatcher::lockedChanged};
    maru::test::pumpFor(300);
    ASSERT_FALSE(watcher.isLocked());

    screenSaver->setActive(true);
    ASSERT_TRUE(spy.wait(2000));
    EXPECT_TRUE(watcher.isLocked());

    screenSaver->setActive(false);
    ASSERT_TRUE(spy.wait(2000));
    EXPECT_FALSE(watcher.isLocked());

    // Locking and unlocking once each is two announcements, and neither repeated value adds a
    // third.
    screenSaver->setActive(false);
    maru::test::pumpFor(200);
    EXPECT_EQ(spy.count(), 2);
}

TEST_F(LockWatcherFakeTest, answersALockRequestAsALockedSession)
{
    ScreenSaverLockWatcher watcher;
    QSignalSpy spy{&watcher, &LockWatcher::lockedChanged};
    maru::test::pumpFor(300);

    // Lock() is the method a screen locker calls, and it drives ActiveChanged the same way a
    // user locking the session does.
    screenSaver->Lock();
    ASSERT_TRUE(spy.wait(2000));
    EXPECT_TRUE(watcher.isLocked());
}

TEST_F(LockWatcherFakeTest, staysUnlockedWhereGetActiveAnswersAnError)
{
    screenSaver->failGetActive = true;

    ScreenSaverLockWatcher watcher;
    QSignalSpy spy{&watcher, &LockWatcher::lockedChanged};
    maru::test::pumpFor(500);

    // A name on the bus with no working implementation leaves scanning running rather than
    // pausing it for a session that is not locked.
    EXPECT_FALSE(watcher.isLocked());
    EXPECT_EQ(spy.count(), 0);
}

TEST_F(LockWatcherFakeTest, rereadsTheStateOnAnExplicitQuery)
{
    ScreenSaverLockWatcher watcher;
    maru::test::pumpFor(300);
    const int answered = screenSaver->getActiveCount();

    watcher.query();
    maru::test::pumpFor(300);

    EXPECT_EQ(screenSaver->getActiveCount(), answered + 1);
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
