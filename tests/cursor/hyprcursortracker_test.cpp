// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The Hyprland pointer source: the socket path, the reply parser and the poll.
//
// The poll runs against maru::test::FakeHyprSocket, which listens on the path
// cursor::hyprSocketPath() builds under a temporary runtime directory, so the whole request,
// reply, parse and emit chain executes on a host running no Hyprland.
#include "cursor/hyprcursortracker.h"
#include "cursor/hyprsocket.h"
#include "eventloop.h"
#include "fakehyprsocket.h"

#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace maru::cursor;

namespace
{

class HyprCursorTrackerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(runtime.isValid());
        socket = new maru::test::FakeHyprSocket(runtime.path(), QStringLiteral("marupop_test_0"));
        if (!socket->isListening()) {
            GTEST_SKIP() << socket->skipReason().toStdString();
        }
    }

    void TearDown() override
    {
        delete socket;
        socket = nullptr;
    }

    // A tracker pointed at the fake, polling fast enough that a case waits milliseconds rather
    // than the 8 ms and 500 ms defaults.
    [[nodiscard]] HyprCursorTracker *makeTracker()
    {
        auto *tracker = new HyprCursorTracker(socket->socketPath(), &owner);
        tracker->setTrackingIntervalMs(2);
        tracker->setIdleIntervalMs(5);
        return tracker;
    }

    QTemporaryDir runtime;
    maru::test::FakeHyprSocket *socket = nullptr;
    QObject owner;
};

} // namespace

TEST(HyprSocketTest, parsesThePlainReply)
{
    EXPECT_EQ(parseCursorPos(QByteArrayLiteral("1920, 540")), QPoint(1920, 540));
    EXPECT_EQ(parseCursorPos(QByteArrayLiteral("  -12,-7  ")), QPoint(-12, -7));
}

TEST(HyprSocketTest, parsesTheJsonReply)
{
    EXPECT_EQ(parseCursorPos(QByteArrayLiteral("{\n    \"x\": 42,\n    \"y\": 7\n}\n")), QPoint(42, 7));
}

TEST(HyprSocketTest, refusesAReplyOfAnotherShape)
{
    EXPECT_FALSE(parseCursorPos(QByteArray{}).has_value());
    EXPECT_FALSE(parseCursorPos(QByteArrayLiteral("unknown request")).has_value());
    EXPECT_FALSE(parseCursorPos(QByteArrayLiteral("1920")).has_value());
    EXPECT_FALSE(parseCursorPos(QByteArrayLiteral("a, b")).has_value());
    EXPECT_FALSE(parseCursorPos(QByteArrayLiteral("{\"x\": 1}")).has_value());
}

TEST_F(HyprCursorTrackerTest, readsThePointerPositionFromTheSocket)
{
    socket->setCursorPos(QPoint(1234, 567));
    HyprCursorTracker *tracker = makeTracker();
    QSignalSpy positions{tracker, &CursorTracker::positionChanged};
    tracker->start();

    ASSERT_TRUE(positions.wait(2000));
    EXPECT_EQ(positions.constFirst().at(0).toPoint(), QPoint(1234, 567));
    EXPECT_EQ(tracker->lastPosition(), QPoint(1234, 567));
    EXPECT_TRUE(tracker->isAvailable());
    EXPECT_TRUE(tracker->unavailableReason().isEmpty());
    EXPECT_EQ(socket->commands().constFirst(), QStringLiteral("cursorpos"));
}

TEST_F(HyprCursorTrackerTest, readsTheJsonReplyForm)
{
    socket->setJsonReplies(true);
    socket->setCursorPos(QPoint(11, 22));
    HyprCursorTracker *tracker = makeTracker();
    QSignalSpy positions{tracker, &CursorTracker::positionChanged};
    tracker->start();

    ASSERT_TRUE(positions.wait(2000));
    EXPECT_EQ(positions.constFirst().at(0).toPoint(), QPoint(11, 22));
}

TEST_F(HyprCursorTrackerTest, emitsOnceForAPointerThatDidNotMove)
{
    socket->setCursorPos(QPoint(100, 100));
    HyprCursorTracker *tracker = makeTracker();
    QSignalSpy positions{tracker, &CursorTracker::positionChanged};
    tracker->start();
    ASSERT_TRUE(positions.wait(2000));

    // Several polls answer the same point. positionChanged carries a change, so the popup's
    // anchor is not driven at the poll rate by a pointer at rest.
    const int before = positions.size();
    maru::test::pumpFor(60);
    EXPECT_EQ(positions.size(), before);
    EXPECT_GT(socket->requestCount(), before) << "the poll stopped instead of answering the same point";
}

TEST_F(HyprCursorTrackerTest, followsAPointerThatMoves)
{
    socket->setCursorPos(QPoint(0, 0));
    HyprCursorTracker *tracker = makeTracker();
    QSignalSpy positions{tracker, &CursorTracker::positionChanged};
    tracker->start();
    ASSERT_TRUE(positions.wait(2000));

    socket->setCursorPos(QPoint(640, 480));
    ASSERT_TRUE(positions.wait(2000));
    EXPECT_EQ(tracker->lastPosition(), QPoint(640, 480));
}

TEST_F(HyprCursorTrackerTest, pollsFasterWhileTracking)
{
    socket->setCursorPos(QPoint(5, 5));
    HyprCursorTracker *tracker = makeTracker();
    tracker->start();
    maru::test::pumpFor(60);
    const int idleRequests = socket->requestCount();

    tracker->setTracking(true);
    maru::test::pumpFor(60);
    const int trackingRequests = socket->requestCount() - idleRequests;

    // 5 ms idle against 2 ms tracking over the same window. The comparison is of counts rather
    // than of rates, so a loaded machine that missed ticks in both windows still passes.
    EXPECT_GT(trackingRequests, idleRequests)
        << "idle produced " << idleRequests << " requests and tracking produced " << trackingRequests;
}

TEST_F(HyprCursorTrackerTest, reportsUnavailableWhereTheSocketAnswersNothing)
{
    socket->setSilent(true);
    HyprCursorTracker *tracker = makeTracker();
    QSignalSpy availability{tracker, &CursorTracker::availabilityChanged};
    tracker->start();

    ASSERT_TRUE(availability.wait(3000));
    EXPECT_FALSE(availability.constLast().at(0).toBool());
    EXPECT_FALSE(availability.constLast().at(1).toString().isEmpty());
    EXPECT_FALSE(tracker->isAvailable());
}

TEST_F(HyprCursorTrackerTest, reportsUnavailableWhereTheSocketIsGone)
{
    socket->setCursorPos(QPoint(1, 1));
    HyprCursorTracker *tracker = makeTracker();
    QSignalSpy positions{tracker, &CursorTracker::positionChanged};
    tracker->start();
    ASSERT_TRUE(positions.wait(2000));

    // A compositor exit takes the socket file with it.
    QSignalSpy availability{tracker, &CursorTracker::availabilityChanged};
    socket->stop();
    ASSERT_TRUE(availability.wait(3000));
    EXPECT_FALSE(availability.constLast().at(0).toBool());
    EXPECT_TRUE(availability.constLast().at(1).toString().contains(socket->socketPath()));
}

TEST_F(HyprCursorTrackerTest, stopsPollingOnStop)
{
    HyprCursorTracker *tracker = makeTracker();
    tracker->start();
    maru::test::pumpFor(40);
    tracker->stop();
    // A request already written when stop() ran still reaches the server: abort() closes this
    // side of a connection the server has already accepted. One short pump drains that one, and
    // the count is taken after it.
    maru::test::pumpFor(20);
    const int drained = socket->requestCount();
    maru::test::pumpFor(60);
    EXPECT_EQ(socket->requestCount(), drained);
}

TEST_F(HyprCursorTrackerTest, deliversNoPositionForARequestItAbandoned)
{
    // A reply arriving after the client gave up must not be delivered. Qt can emit
    // readyRead followed by errorOccurred(PeerClosedError), with disconnected() emitted
    // by the handler's abort. Neither signal may re-enter completion after cancellation
    // and reset the failure count for an abandoned request.
    socket->setCursorPos(QPoint(4242, 2424));
    // Four poll intervals is the timeout, so a reply three times that is abandoned twice over.
    socket->setReplyDelayMs(60);
    HyprCursorTracker *tracker = makeTracker();
    tracker->setTrackingIntervalMs(5);
    QSignalSpy positions{tracker, &CursorTracker::positionChanged};
    QSignalSpy availability{tracker, &CursorTracker::availabilityChanged};
    tracker->setTracking(true);
    tracker->start();

    ASSERT_TRUE(availability.wait(5000)) << "the tracker never reported the outage";
    EXPECT_FALSE(availability.constLast().at(0).toBool());
    EXPECT_TRUE(positions.isEmpty()) << "a position was delivered for an abandoned request";
    EXPECT_EQ(tracker->sampleCount(), 0U);

    // And it recovers: once the compositor answers inside the timeout, the poll delivers again.
    socket->setReplyDelayMs(0);
    ASSERT_TRUE(positions.wait(5000)) << "the poll never recovered after the outage";
    EXPECT_EQ(tracker->lastPosition(), QPoint(4242, 2424));
    EXPECT_TRUE(tracker->isAvailable());
}

TEST(HyprCursorTrackerPathTest, reportsUnavailableWithNoInstanceSignature)
{
    HyprCursorTracker tracker{QString{}};
    QSignalSpy availability{&tracker, &CursorTracker::availabilityChanged};
    tracker.start();
    ASSERT_EQ(availability.size(), 1);
    EXPECT_FALSE(availability.constFirst().at(0).toBool());
    EXPECT_TRUE(tracker.socketPath().isEmpty());
}

int main(int argc, char **argv)
{
    QGuiApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
