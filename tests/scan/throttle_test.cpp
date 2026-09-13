// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The trailing-edge throttle and the poll timer. The elapsed-time decisions run on an injected
// clock, so only the delivery of the trailing fire depends on the event loop.
#include "scan/throttle.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QGuiApplication>

#include <functional>
#include <gtest/gtest.h>

using namespace maru::scan;

namespace
{

void pump(const std::function<bool()> &done, int timeoutMs = 5000)
{
    const QDeadlineTimer deadline{timeoutMs};
    while (!done() && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
}

void wait(int ms)
{
    const QDeadlineTimer deadline{ms};
    while (!deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
}

} // namespace

TEST(ThrottleTest, firesTheFirstRequestImmediately)
{
    Throttle throttle;
    qint64 now = 1000;
    throttle.setClock([&now] {
        return now;
    });
    throttle.setIntervalMs(200);

    int fires = 0;
    QObject::connect(&throttle, &Throttle::triggered, &throttle, [&fires] {
        ++fires;
    });

    throttle.request();
    EXPECT_EQ(fires, 1);
    EXPECT_FALSE(throttle.isPending());
}

TEST(ThrottleTest, coalescesEveryRequestInsideTheWindowIntoOneTrailingFire)
{
    Throttle throttle;
    qint64 now = 0;
    throttle.setClock([&now] {
        return now;
    });
    throttle.setIntervalMs(60);

    int fires = 0;
    QObject::connect(&throttle, &Throttle::triggered, &throttle, [&fires] {
        ++fires;
    });

    throttle.request();
    ASSERT_EQ(fires, 1);

    // The 8 ms pointer pump the KWin relay runs at, inside one window.
    for (int step = 1; step <= 5; ++step) {
        now = static_cast<qint64>(step) * 8;
        throttle.request();
    }
    EXPECT_EQ(fires, 1);
    EXPECT_TRUE(throttle.isPending());

    pump([&fires] {
        return fires >= 2;
    });
    EXPECT_EQ(fires, 2);
    EXPECT_FALSE(throttle.isPending());
}

TEST(ThrottleTest, schedulesTheTrailingFireForTheRestOfTheWindow)
{
    Throttle throttle;
    qint64 now = 0;
    throttle.setClock([&now] {
        return now;
    });
    throttle.setIntervalMs(200);

    int fires = 0;
    QElapsedTimer elapsed;
    QObject::connect(&throttle, &Throttle::triggered, &throttle, [&fires] {
        ++fires;
    });

    throttle.request();
    ASSERT_EQ(fires, 1);

    // 50 ms into the window, so 150 ms of it are left.
    now = 50;
    elapsed.start();
    throttle.request();
    pump([&fires] {
        return fires >= 2;
    });
    ASSERT_EQ(fires, 2);
    // A generous upper bound: the lower one is what the test is about, and a loaded machine
    // may deliver the timeout late.
    EXPECT_GE(elapsed.elapsed(), 120);
    EXPECT_LT(elapsed.elapsed(), 2000);
}

TEST(ThrottleTest, firesImmediatelyOnceTheWindowHasPassed)
{
    Throttle throttle;
    qint64 now = 0;
    throttle.setClock([&now] {
        return now;
    });
    throttle.setIntervalMs(100);

    int fires = 0;
    QObject::connect(&throttle, &Throttle::triggered, &throttle, [&fires] {
        ++fires;
    });

    throttle.request();
    now = 100;
    throttle.request();
    EXPECT_EQ(fires, 2);
    EXPECT_FALSE(throttle.isPending());
}

TEST(ThrottleTest, cancelDropsTheScheduledFire)
{
    Throttle throttle;
    qint64 now = 0;
    throttle.setClock([&now] {
        return now;
    });
    throttle.setIntervalMs(50);

    int fires = 0;
    QObject::connect(&throttle, &Throttle::triggered, &throttle, [&fires] {
        ++fires;
    });

    throttle.request();
    now = 10;
    throttle.request();
    ASSERT_TRUE(throttle.isPending());

    throttle.cancel();
    EXPECT_FALSE(throttle.isPending());
    wait(150);
    EXPECT_EQ(fires, 1);
}

TEST(ThrottleTest, clampsTheIntervalToOneMillisecond)
{
    Throttle throttle;
    throttle.setIntervalMs(0);
    EXPECT_EQ(throttle.intervalMs(), 1);
    throttle.setIntervalMs(-5);
    EXPECT_EQ(throttle.intervalMs(), 1);
}

TEST(PeriodicPollerTest, ticksUntilStopped)
{
    PeriodicPoller poller;
    poller.setIntervalMs(10);
    EXPECT_EQ(poller.intervalMs(), 10);
    EXPECT_FALSE(poller.isActive());

    int ticks = 0;
    QObject::connect(&poller, &PeriodicPoller::tick, &poller, [&ticks] {
        ++ticks;
    });

    poller.start();
    EXPECT_TRUE(poller.isActive());
    pump([&ticks] {
        return ticks >= 3;
    });
    ASSERT_GE(ticks, 3);

    poller.stop();
    EXPECT_FALSE(poller.isActive());
    const int stopped = ticks;
    wait(60);
    EXPECT_EQ(ticks, stopped);
}

int main(int argc, char **argv)
{
    // QGuiApplication rather than QCoreApplication so every suite in tests/scan/ runs on the
    // same platform plugin, and because the timers under test are driven by its event loop.
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
