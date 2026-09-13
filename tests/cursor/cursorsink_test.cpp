// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The D-Bus object the KWin relay script calls. Update is invoked over the session bus rather
// than called directly, so the exported signature is covered as well as the slot body: KWin's
// callDBus() infers the signature from the JS values it is given, and a mismatch would leave
// the script calling a method the bus reports as unknown.
//
// The service name is unique per process, because io.github.marunine.marupop is owned by a
// running MaruPop through KDBusService.
#include "cursor/cursorsink.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QEventLoop>
#include <QGuiApplication>
#include <QScreen>
#include <QSignalSpy>
#include <QTimer>

#include <gtest/gtest.h>
#include <optional>

using namespace maru::cursor;

namespace
{

QString testServiceName()
{
    return QStringLiteral("io.github.marunine.marupop.test%1").arg(QCoreApplication::applicationPid());
}

// Calls Update over the bus and returns the reply, or nullopt on an error or a timeout. The
// call is asynchronous and the event loop is pumped, because the receiver is this process:
// a blocking call would have to dispatch its own incoming message.
std::optional<bool> callUpdate(int x, int y, const QString &screenName, const QVariant &dpr, double sentMs)
{
    QDBusMessage message = QDBusMessage::createMethodCall(
        testServiceName(), CursorSink::objectPath(), CursorSink::interfaceName(), QStringLiteral("Update"));
    message.setArguments({x, y, screenName, dpr, sentMs});

    QDBusPendingCallWatcher watcher{QDBusConnection::sessionBus().asyncCall(message, 5000)};
    QEventLoop loop;
    QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    if (!watcher.isFinished()) {
        loop.exec();
    }
    const QDBusPendingReply<bool> reply = watcher;
    if (reply.isError()) {
        qWarning("Update failed: %s", qPrintable(reply.error().message()));
        return std::nullopt;
    }
    return reply.value();
}

class CursorSinkTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!QDBusConnection::sessionBus().isConnected()) {
            GTEST_SKIP() << "no session bus";
        }
        sink = std::make_unique<CursorSink>();
        ASSERT_TRUE(sink->registerObject(QDBusConnection::sessionBus()));
        ASSERT_TRUE(QDBusConnection::sessionBus().registerService(testServiceName()));
    }

    void TearDown() override
    {
        if (sink) {
            QDBusConnection::sessionBus().unregisterService(testServiceName());
            sink.reset();
        }
    }

    std::unique_ptr<CursorSink> sink;
};

double nowMs()
{
    return static_cast<double>(QDateTime::currentMSecsSinceEpoch());
}

} // namespace

TEST_F(CursorSinkTest, namesTheObjectAndInterfaceTheScriptCalls)
{
    EXPECT_EQ(CursorSink::objectPath(), QStringLiteral("/Cursor"));
    EXPECT_EQ(CursorSink::interfaceName(), QStringLiteral("io.github.marunine.marupop.CursorSink"));
}

TEST_F(CursorSinkTest, emitsThePositionItWasCalledWith)
{
    QScreen *screen = QGuiApplication::primaryScreen();
    ASSERT_NE(screen, nullptr);
    const QPoint point = screen->geometry().center();

    // A lambda rather than QSignalSpy for the screen argument: QSignalSpy records a QScreen*
    // as a QVariant whose conversion back to QScreen* yields nullptr.
    int emissions = 0;
    QPoint emittedPoint;
    QScreen *emittedScreen = nullptr;
    QObject::connect(sink.get(), &CursorSink::positionChanged, sink.get(), [&](QPoint p, QScreen *s) {
        ++emissions;
        emittedPoint = p;
        emittedScreen = s;
    });
    const std::optional<bool> reply = callUpdate(point.x(), point.y(), screen->name(), 1.0, nowMs());
    ASSERT_TRUE(reply.has_value());

    ASSERT_EQ(emissions, 1);
    EXPECT_EQ(emittedPoint, point);
    EXPECT_EQ(emittedScreen, screen);
    EXPECT_EQ(sink->lastPosition(), point);
    EXPECT_EQ(sink->lastScreen(), screen);
}

TEST_F(CursorSinkTest, dropsARepeatedPosition)
{
    const QScreen *screen = QGuiApplication::primaryScreen();
    const QPoint point = screen->geometry().center();

    QSignalSpy spy{sink.get(), &CursorSink::positionChanged};
    ASSERT_TRUE(callUpdate(point.x(), point.y(), screen->name(), 1.0, nowMs()).has_value());
    ASSERT_TRUE(callUpdate(point.x(), point.y(), screen->name(), 1.0, nowMs()).has_value());
    EXPECT_EQ(spy.count(), 1);

    // A different position is reported again.
    ASSERT_TRUE(callUpdate(point.x() + 1, point.y(), screen->name(), 1.0, nowMs()).has_value());
    EXPECT_EQ(spy.count(), 2);
}

TEST_F(CursorSinkTest, returnsTheTrackingFlagToTheScript)
{
    const QScreen *screen = QGuiApplication::primaryScreen();
    const QPoint point = screen->geometry().center();

    EXPECT_FALSE(sink->isTracking());
    std::optional<bool> reply = callUpdate(point.x(), point.y(), screen->name(), 1.0, nowMs());
    ASSERT_TRUE(reply.has_value());
    EXPECT_FALSE(*reply);

    sink->setTracking(true);
    reply = callUpdate(point.x() + 1, point.y(), screen->name(), 1.0, nowMs());
    ASSERT_TRUE(reply.has_value());
    EXPECT_TRUE(*reply);

    sink->setTracking(false);
    reply = callUpdate(point.x() + 2, point.y(), screen->name(), 1.0, nowMs());
    ASSERT_TRUE(reply.has_value());
    EXPECT_FALSE(*reply);
}

TEST_F(CursorSinkTest, resolvesTheScreenFromThePositionWhenTheNameDisagrees)
{
    QScreen *screen = QGuiApplication::primaryScreen();
    const QPoint point = screen->geometry().center();

    // A name no output has: QGuiApplication::screenAt() is the primary answer and the name is
    // only the cross-check, so the position still resolves.
    ASSERT_TRUE(callUpdate(point.x(), point.y(), QStringLiteral("no-such-output"), 1.0, nowMs()).has_value());
    EXPECT_EQ(sink->lastScreen(), screen);

    // A position on no output resolves to no screen.
    ASSERT_TRUE(callUpdate(-100000, -100000, QStringLiteral("no-such-output"), 1.0, nowMs()).has_value());
    EXPECT_EQ(sink->lastScreen(), nullptr);
}

TEST_F(CursorSinkTest, recordsTheDeliveryLatency)
{
    const QScreen *screen = QGuiApplication::primaryScreen();
    const QPoint point = screen->geometry().center();

    EXPECT_EQ(sink->latency().samples, 0U);
    ASSERT_TRUE(callUpdate(point.x(), point.y(), screen->name(), 1.5, nowMs()).has_value());
    ASSERT_TRUE(callUpdate(point.x() + 1, point.y(), screen->name(), 1.5, nowMs()).has_value());

    const CursorSink::LatencyStats stats = sink->latency();
    EXPECT_EQ(stats.samples, 2U);
    EXPECT_LE(stats.minMs, stats.meanMs);
    EXPECT_LE(stats.meanMs, stats.maxMs);
    // A message sent and received inside one process takes less than a second.
    EXPECT_LT(stats.maxMs, 1000.0);
    EXPECT_DOUBLE_EQ(sink->lastDevicePixelRatio(), 1.5);

    sink->resetLatency();
    EXPECT_EQ(sink->latency().samples, 0U);
}

TEST_F(CursorSinkTest, acceptsAnIntegerDevicePixelRatio)
{
    QScreen *screen = QGuiApplication::primaryScreen();
    const QPoint point = screen->geometry().center();

    // KWin marshals a scale of 1 as int32, because QJSValue::toVariant() returns an int for an
    // integral JS number. A sink exporting only the double form answers "No such method
    // 'Update' ... (signature 'iisid')".
    const std::optional<bool> reply = callUpdate(point.x(), point.y(), screen->name(), QVariant(1), nowMs());
    ASSERT_TRUE(reply.has_value());
    EXPECT_EQ(sink->lastScreen(), screen);
    EXPECT_DOUBLE_EQ(sink->lastDevicePixelRatio(), 1.0);

    // The fractional form an output at scale 1.25 produces reaches the same slot.
    ASSERT_TRUE(callUpdate(point.x() + 1, point.y(), screen->name(), QVariant(1.25), nowMs()).has_value());
    EXPECT_DOUBLE_EQ(sink->lastDevicePixelRatio(), 1.25);
}

TEST_F(CursorSinkTest, unregistersTheObjectOnRequest)
{
    EXPECT_TRUE(sink->isRegistered());
    sink->unregisterObject();
    EXPECT_FALSE(sink->isRegistered());
    // With the object gone the bus answers with an error rather than a value.
    EXPECT_FALSE(callUpdate(1, 1, QStringLiteral("x"), 1.0, nowMs()).has_value());
}

int main(int argc, char **argv)
{
    // QGuiApplication rather than QCoreApplication: CursorSink resolves a position through
    // QGuiApplication::screenAt(), which needs a platform plugin and a screen. The offscreen
    // plugin supplies one, and tests/CMakeLists.txt sets QT_QPA_PLATFORM=offscreen.
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
