// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// capture::KWinGrabber against maru::test::FakeKWin, which owns org.kde.KWin on the private
// session bus dbus-run-session gives this binary.
//
// The fake covers the three replies a running compositor produces only under conditions a test
// cannot arrange: org.kde.KWin.ScreenShot2.Error.NoAuthorized, which needs an unauthorized
// executable; org.kde.KWin.ScreenShot2.Error.Cancelled, which needs a superseding grab; and a
// reply describing more bytes than the pipe delivered, which is the KWin 6.7.4 flush loss
// src/capture/kwingrabber.cpp compensates for and which appears only while the session is over
// fs.pipe-user-pages-soft.
//
// The live counterpart is FrameSourceTest.grabsFourHundredByTwoHundredFromKWin, which runs
// against a real compositor inside tests/harness/nested-session.sh.
#include "capture/kwingrabber.h"
#include "fakekwin.h"
#include "privatebus.h"

#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QSignalSpy>
#include <QTimer>

#include <gtest/gtest.h>

using namespace maru;
using namespace maru::capture;
using maru::test::FakeKWin;
using maru::test::FakeScreenShot2;

namespace
{

// The largest tail KWinGrab fills with zero bytes rather than rejecting, from
// src/capture/kwingrabber.cpp kMaxLostTail.
constexpr qsizetype kMaxLostTail = 16384;

// A fixture per test, because each one registers org.kde.KWin and releases it again, and a
// GTest binary runs its cases in one process on one bus.
class KWinGrabberTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        kwin = std::make_unique<FakeKWin>();
        if (!kwin->isRegistered()) {
            GTEST_SKIP() << kwin->skipReason().toStdString();
        }
    }

    void TearDown() override
    {
        kwin.reset();
    }

    [[nodiscard]] FakeScreenShot2 *screenShot() const
    {
        return kwin->screenShot();
    }

    // Runs the event loop until grab emits finished() or failed(), or 5000 ms elapse. Returns
    // the image on success and a null image on failure, with the error in outError.
    QImage await(KWinGrab *grab, KWinGrab::Error *outError = nullptr, QString *outMessage = nullptr)
    {
        QImage image;
        bool done = false;
        QEventLoop loop;
        QObject::connect(grab, &KWinGrab::finished, &loop, [&](const QImage &result) {
            image = result;
            done = true;
            loop.quit();
        });
        QObject::connect(grab, &KWinGrab::failed, &loop, [&](KWinGrab::Error error, const QString &message) {
            if (outError != nullptr) {
                *outError = error;
            }
            if (outMessage != nullptr) {
                *outMessage = message;
            }
            done = true;
            loop.quit();
        });
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        EXPECT_TRUE(done) << "neither finished() nor failed() arrived within 5000 ms";
        return image;
    }

    std::unique_ptr<FakeKWin> kwin;
};

} // namespace

TEST_F(KWinGrabberTest, deliversTheImageTheCompositorWrote)
{
    KWinGrabber grabber;
    const QImage image = await(grabber.captureArea(QRect{10, 20, 64, 64}, {}));

    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.size(), screenShot()->image.size());
    EXPECT_EQ(image.format(), screenShot()->image.format());
    // The default fake image encodes the column index in red and the row index in green, so a
    // stride error shows as a wrong colour rather than as a wrong size.
    EXPECT_EQ(image.pixelColor(0, 0), screenShot()->image.pixelColor(0, 0));
    EXPECT_EQ(image.pixelColor(63, 63), screenShot()->image.pixelColor(63, 63));
}

TEST_F(KWinGrabberTest, sendsTheRequestedRectangleAndTheOptionPolarities)
{
    KWinGrabber grabber;
    KWinGrabber::Options options;
    options.includeCursor = true;
    options.includeOwnWindows = false;
    options.nativeResolution = true;
    await(grabber.captureArea(QRect{11, 22, 33, 44}, options));

    EXPECT_EQ(screenShot()->lastMethod(), QStringLiteral("CaptureArea"));
    ASSERT_EQ(screenShot()->lastArguments().size(), 4);
    EXPECT_EQ(screenShot()->lastArguments().at(0).toInt(), 11);
    EXPECT_EQ(screenShot()->lastArguments().at(1).toInt(), 22);
    EXPECT_EQ(screenShot()->lastArguments().at(2).toUInt(), 33U);
    EXPECT_EQ(screenShot()->lastArguments().at(3).toUInt(), 44U);

    const QVariantMap sent = screenShot()->lastOptions();
    EXPECT_TRUE(sent.value(QStringLiteral("include-cursor")).toBool());
    EXPECT_TRUE(sent.value(QStringLiteral("native-resolution")).toBool());
    // includeOwnWindows reaches ScreenShot2 as its negation, hide-caller-windows.
    EXPECT_TRUE(sent.value(QStringLiteral("hide-caller-windows")).toBool());
}

TEST_F(KWinGrabberTest, appliesTheReplyScaleAsTheDevicePixelRatio)
{
    screenShot()->scale = 2.0;
    KWinGrabber grabber;
    const QImage image = await(grabber.captureArea(QRect{0, 0, 32, 32}, {}));

    ASSERT_FALSE(image.isNull());
    EXPECT_DOUBLE_EQ(image.devicePixelRatio(), 2.0);
}

TEST_F(KWinGrabberTest, reportsPermissionDeniedForNoAuthorized)
{
    screenShot()->failure = FakeScreenShot2::Failure::NoAuthorized;
    KWinGrabber grabber;
    KWinGrab::Error error = KWinGrab::Error::DBus;
    QString message;
    const QImage image = await(grabber.captureArea(QRect{0, 0, 32, 32}, {}), &error, &message);

    EXPECT_TRUE(image.isNull());
    EXPECT_EQ(error, KWinGrab::Error::PermissionDenied);
    // The message names the desktop-entry key, which is the one actionable step for this error.
    EXPECT_TRUE(message.contains(QStringLiteral("X-KDE-DBUS-Restricted-Interfaces"))) << message.toStdString();
}

TEST_F(KWinGrabberTest, reportsCancelledForACancelledGrab)
{
    screenShot()->failure = FakeScreenShot2::Failure::Cancelled;
    KWinGrabber grabber;
    KWinGrab::Error error = KWinGrab::Error::DBus;
    await(grabber.captureArea(QRect{0, 0, 32, 32}, {}), &error);

    EXPECT_EQ(error, KWinGrab::Error::Cancelled);
}

TEST_F(KWinGrabberTest, reportsADBusErrorForAnyOtherErrorName)
{
    screenShot()->failure = FakeScreenShot2::Failure::Unknown;
    KWinGrabber grabber;
    KWinGrab::Error error = KWinGrab::Error::Read;
    await(grabber.captureArea(QRect{0, 0, 32, 32}, {}), &error);

    EXPECT_EQ(error, KWinGrab::Error::DBus);
}

TEST_F(KWinGrabberTest, fillsATailOfAtMostSixteenKibibytesWithBlack)
{
    // The KWin 6.7.4 flush loss: the reply describes the whole image while the pipe carried
    // less. One row of the 64-pixel-wide default image is 256 bytes, so 2560 bytes is the last
    // ten rows, well inside the 16384-byte allowance.
    constexpr qsizetype withheld = 2560;
    constexpr int rowsLost = 10;
    screenShot()->withholdBytes = withheld;

    KWinGrabber grabber;
    const QImage image = await(grabber.captureArea(QRect{0, 0, 64, 64}, {}));

    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.size(), QSize(64, 64));
    // The rows that arrived keep their values.
    EXPECT_EQ(image.pixelColor(0, 0), screenShot()->image.pixelColor(0, 0));
    EXPECT_EQ(image.pixelColor(63, 63 - rowsLost), screenShot()->image.pixelColor(63, 63 - rowsLost));
    // The rows that did not arrive read as zero in every channel, which is opaque black in
    // Format_RGBA8888 only where alpha is also zero, so the comparison is on the raw pixel.
    EXPECT_EQ(image.pixel(0, 63) & 0x00FFFFFFU, 0U);
}

TEST_F(KWinGrabberTest, rejectsATailLargerThanSixteenKibibytes)
{
    // 128x128 in Format_RGBA8888 is 65536 bytes, so a withheld tail of 16640 bytes leaves 48896
    // bytes delivered and a shortfall past the allowance. The 64x64 default image is 16384
    // bytes in total, which no shortfall can exceed.
    QImage large(128, 128, QImage::Format_RGBA8888);
    large.fill(Qt::red);
    screenShot()->image = large;
    screenShot()->withholdBytes = kMaxLostTail + 256;

    KWinGrabber grabber;
    KWinGrab::Error error = KWinGrab::Error::DBus;
    QString message;
    const QImage image = await(grabber.captureArea(QRect{0, 0, 128, 128}, {}), &error, &message);

    EXPECT_TRUE(image.isNull());
    EXPECT_EQ(error, KWinGrab::Error::Read);
    EXPECT_TRUE(message.contains(QStringLiteral("invalid image reply"))) << message.toStdString();
}

TEST_F(KWinGrabberTest, drainsAnImageLargerThanOnePipeBuffer)
{
    // 4096x1024 in Format_RGBA8888 is 16 MiB, well past the 1 MiB an unprivileged
    // F_SETPIPE_SZ may ask for, so the reply arrives while the reader is still draining. A
    // sequence that waited for the reply before reading would deadlock here.
    QImage large(4096, 1024, QImage::Format_RGBA8888);
    large.fill(QColor(12, 34, 56, 255));
    screenShot()->image = large;

    KWinGrabber grabber;
    const QImage image = await(grabber.captureArea(QRect{0, 0, 4096, 1024}, {}));

    ASSERT_FALSE(image.isNull());
    EXPECT_EQ(image.size(), QSize(4096, 1024));
    EXPECT_EQ(image.pixelColor(4095, 1023), QColor(12, 34, 56, 255));
}

TEST_F(KWinGrabberTest, callsTheMethodEachCaptureEntryPointNames)
{
    KWinGrabber grabber;

    await(grabber.captureWorkspace({}));
    EXPECT_EQ(screenShot()->lastMethod(), QStringLiteral("CaptureWorkspace"));

    await(grabber.captureScreen(QStringLiteral("DP-1"), {}));
    EXPECT_EQ(screenShot()->lastMethod(), QStringLiteral("CaptureScreen"));
    EXPECT_EQ(screenShot()->lastArguments().value(0).toString(), QStringLiteral("DP-1"));

    await(grabber.captureActiveScreen({}));
    EXPECT_EQ(screenShot()->lastMethod(), QStringLiteral("CaptureActiveScreen"));

    await(grabber.captureActiveWindow({}));
    EXPECT_EQ(screenShot()->lastMethod(), QStringLiteral("CaptureActiveWindow"));

    await(grabber.captureWindow(QStringLiteral("{uuid}"), {}));
    EXPECT_EQ(screenShot()->lastMethod(), QStringLiteral("CaptureWindow"));

    EXPECT_EQ(screenShot()->callCount(), 5);
}

TEST_F(KWinGrabberTest, reportsTheServiceAsAvailableWhileTheNameIsOwned)
{
    EXPECT_TRUE(KWinGrabber::serviceAvailable());
}

TEST(KWinGrabberWithoutServiceTest, reportsADBusFailureWhereNoServiceOwnsTheName)
{
    if (!maru::test::privateBusAvailable()) {
        GTEST_SKIP() << maru::test::privateBusSkipReason().toStdString();
    }
    // This case constructs no FakeKWin, so org.kde.KWin is unowned and the bus answers
    // org.freedesktop.DBus.Error.ServiceUnknown. The precondition is checked rather than
    // assumed: it holds because every KWinGrabberTest case released the name in its TearDown,
    // and --gtest_shuffle can put this case between two of them.
    if (KWinGrabber::serviceAvailable()) {
        GTEST_SKIP() << "org.kde.KWin is owned on this bus, which a shuffled run puts a FakeKWin "
                        "in the way of; run without --gtest_shuffle";
    }

    KWinGrabber grabber;
    KWinGrab *grab = grabber.captureArea(QRect{0, 0, 32, 32}, {});
    QSignalSpy failures{grab, &KWinGrab::failed};
    EXPECT_TRUE(failures.wait(5000));
    ASSERT_EQ(failures.count(), 1);
    EXPECT_EQ(failures.first().at(0).value<KWinGrab::Error>(), KWinGrab::Error::DBus);
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
