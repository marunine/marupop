// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The frame hash, the latest-wins request queue, and one live grab.
//
// The live case runs only where org.kde.KWin owns its bus name and MARUPOP_LIVE_CAPTURE=1 is
// set, because it renders the tester's screen. It also needs an installed desktop entry whose
// Exec names this binary; without one KWin answers every CaptureArea with NoAuthorized.
#include "capture/framesource.h"
#include "capture/scanregion.h"
#include "eventloop.h"
#include "kwinsession.h"

#include <QDebug>
#include <QGuiApplication>
#include <QImage>
#include <QRect>
#include <QScreen>
#include <QSignalSpy>

#include <gtest/gtest.h>

using namespace maru::capture;
using maru::test::waitFor;

namespace
{

// An RGB888 image of an odd width, so QImage pads every row to the next 4-byte boundary and the
// hash has padding bytes it must not read. The buffer is owned by the caller.
QImage paddedImage(QByteArray &buffer, uchar padding, uchar pixel)
{
    constexpr int width = 5;
    constexpr int height = 3;
    constexpr qsizetype stride = 16; // 15 bytes of pixels plus 1 byte of padding
    buffer.fill(static_cast<char>(padding), stride * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width * 3; ++x) {
            buffer[y * stride + x] = static_cast<char>(pixel + static_cast<uchar>(x));
        }
    }
    return {reinterpret_cast<const uchar *>(buffer.constData()), width, height, stride, QImage::Format_RGB888};
}

bool liveCaptureRequested()
{
    return qEnvironmentVariable("MARUPOP_LIVE_CAPTURE") == QLatin1StringView("1");
}

} // namespace

TEST(FrameSourceTest, hashesIdenticalImagesEqually)
{
    QImage first{QSize(64, 48), QImage::Format_ARGB32_Premultiplied};
    first.fill(Qt::darkCyan);
    QImage second{QSize(64, 48), QImage::Format_ARGB32_Premultiplied};
    second.fill(Qt::darkCyan);

    EXPECT_EQ(FrameSource::hashImage(first), FrameSource::hashImage(second));
    EXPECT_NE(FrameSource::hashImage(first), 0U);
}

TEST(FrameSourceTest, hashesOnePixelChangeDifferently)
{
    QImage first{QSize(64, 48), QImage::Format_ARGB32_Premultiplied};
    first.fill(Qt::darkCyan);
    QImage second = first.copy();
    second.setPixelColor(33, 21, Qt::white);

    EXPECT_NE(FrameSource::hashImage(first), FrameSource::hashImage(second));
}

TEST(FrameSourceTest, ignoresTheRowPaddingQImageLeaves)
{
    // Two images with the same pixels and different padding bytes. A hash over
    // bytesPerLine() * height would report them as different frames on every grab.
    QByteArray zeroPadded;
    QByteArray onePadded;
    const QImage first = paddedImage(zeroPadded, 0x00, 0x10);
    const QImage second = paddedImage(onePadded, 0xFF, 0x10);
    ASSERT_EQ(first.bytesPerLine(), 16);
    EXPECT_EQ(FrameSource::hashImage(first), FrameSource::hashImage(second));

    QByteArray changed;
    const QImage third = paddedImage(changed, 0x00, 0x11);
    EXPECT_NE(FrameSource::hashImage(first), FrameSource::hashImage(third));
}

TEST(FrameSourceTest, hashesANullImageAsZero)
{
    EXPECT_EQ(FrameSource::hashImage(QImage()), 0U);
}

TEST(FrameSourceTest, deliversTheSuppliedImageWithItsRectAndScale)
{
    FakeFrameSource source;
    QImage image{QSize(80, 40), QImage::Format_ARGB32_Premultiplied};
    image.fill(Qt::magenta);
    source.setImage(image, 2.0);

    QSignalSpy spy{&source, &FrameSource::frameReady};
    source.grab(QRect(100, 200, 40, 20));
    ASSERT_TRUE(waitFor([&spy] {
        return spy.count() == 1;
    }));

    const auto frame = spy.at(0).at(0).value<Frame>();
    EXPECT_EQ(frame.logicalRect, QRect(100, 200, 40, 20));
    EXPECT_DOUBLE_EQ(frame.scale, 2.0);
    EXPECT_EQ(frame.image.size(), QSize(80, 40));
    EXPECT_EQ(frame.hash, FrameSource::hashImage(image));
    EXPECT_GE(frame.grabMs, 0);
}

TEST(FrameSourceTest, keepsOnlyTheNewestRequestMadeDuringAGrab)
{
    FakeFrameSource source;
    QImage image{QSize(8, 8), QImage::Format_ARGB32_Premultiplied};
    image.fill(Qt::black);
    source.setImage(image);

    const QRect first{0, 0, 100, 100};
    const QRect second{10, 10, 100, 100};
    const QRect third{20, 20, 100, 100};
    source.grab(first);  // issued
    source.grab(second); // remembered
    source.grab(third);  // replaces the remembered rect

    QSignalSpy spy{&source, &FrameSource::frameReady};
    ASSERT_TRUE(waitFor([&spy] {
        return spy.count() == 2;
    }));

    EXPECT_EQ(source.requestedRects(), QList<QRect>({first, second, third}));
    EXPECT_EQ(source.issuedRects(), QList<QRect>({first, third}));
    EXPECT_EQ(spy.at(0).at(0).value<Frame>().logicalRect, first);
    EXPECT_EQ(spy.at(1).at(0).value<Frame>().logicalRect, third);
}

TEST(FrameSourceTest, issuesARequestMadeFromTheFrameReadyHandler)
{
    FakeFrameSource source;
    QImage image{QSize(8, 8), QImage::Format_ARGB32_Premultiplied};
    image.fill(Qt::black);
    source.setImage(image);

    const QRect first{0, 0, 100, 100};
    const QRect second{50, 50, 100, 100};
    int frames = 0;
    QObject::connect(&source, &FrameSource::frameReady, &source, [&](const Frame &) {
        ++frames;
        if (frames == 1) {
            source.grab(second);
        }
    });
    source.grab(first);
    ASSERT_TRUE(waitFor([&frames] {
        return frames == 2;
    }));
    EXPECT_EQ(source.issuedRects(), QList<QRect>({first, second}));
}

TEST(FrameSourceTest, reportsAFailureAndKeepsServingRequests)
{
    FakeFrameSource source;
    source.setFailure(QStringLiteral("no compositor"));

    QSignalSpy failures{&source, &FrameSource::failed};
    source.grab(QRect(0, 0, 10, 10));
    ASSERT_TRUE(waitFor([&failures] {
        return failures.count() == 1;
    }));
    EXPECT_EQ(failures.at(0).at(0).toString(), QStringLiteral("no compositor"));

    // The in-flight marker is cleared on a failure as well, so a second request is issued.
    source.grab(QRect(0, 0, 20, 20));
    ASSERT_TRUE(waitFor([&failures] {
        return failures.count() == 2;
    }));
    EXPECT_EQ(source.issuedRects().size(), 2);
}

TEST(FrameSourceTest, grabsFourHundredByTwoHundredFromKWin)
{
    // Under the harness every precondition of this case is something the harness supplies: it
    // starts the compositor, writes the desktop entry and sets MARUPOP_LIVE_CAPTURE=1. A skip there
    // is therefore a defect in the registration rather than a missing resource, and a silent one:
    // framesource_test_nested would still report as passed, and the CI guard counts ctest-level
    // skips and sees nothing of a GTEST_SKIP. Every gate below is asserted instead.
    const bool underHarness = qEnvironmentVariable("MARUPOP_NESTED_SESSION") == QLatin1StringView("1");
    if (underHarness) {
        ASSERT_TRUE(KWinFrameSource::available()) << "the harness started no compositor that owns org.kde.KWin";
        ASSERT_TRUE(liveCaptureRequested()) << "MARUPOP_LIVE_CAPTURE=1 is unset under the harness; the ENVIRONMENT of "
                                               "maru_add_nested_test(framesource_test_nested …) is what sets it";
    }
    if (!KWinFrameSource::available()) {
        GTEST_SKIP() << "org.kde.KWin does not own its bus name; this is not a KWin Wayland session";
    }
    if (!liveCaptureRequested()) {
        GTEST_SKIP() << "MARUPOP_LIVE_CAPTURE=1 is not set; the live grab renders the tester's screen";
    }
    // The one precondition the harness cannot supply: OpenGL compositing needs a render node on the
    // host, and a container with no GPU has none. The CI step reports this skip as a warning.
    if (const QString reason = maru::test::kwinScreenShotSkipReason(); !reason.isEmpty()) {
        GTEST_SKIP() << reason.toStdString();
    }
    const QScreen *screen = QGuiApplication::primaryScreen();
    ASSERT_NE(screen, nullptr);
    const QRect area{screen->geometry().topLeft() + QPoint(100, 100), QSize(400, 200)};

    KWinFrameSource source;
    QSignalSpy frames{&source, &FrameSource::frameReady};
    QSignalSpy failures{&source, &FrameSource::failed};
    source.grab(area);
    ASSERT_TRUE(waitFor([&frames, &failures] {
        return frames.count() + failures.count() == 1;
    }));

    if (failures.count() == 1) {
        const QString message = failures.at(0).at(0).toString();
        // A denial is a skip on a developer's machine, where writing the entry is a step the tester
        // has to take, and a failure under the harness, which wrote the entry itself.
        if (message.contains(QStringLiteral("desktop entry")) && !underHarness) {
            GTEST_SKIP() << "KWin denied the grab: no installed desktop entry names this binary. "
                            "Run tools/install-dev-desktop.sh <build-dir> to write one.";
        }
        FAIL() << message.toStdString();
    }

    const auto frame = frames.at(0).at(0).value<Frame>();
    EXPECT_EQ(frame.logicalRect, area);
    // native-resolution returns the maximum output scale, which KWinFrameSource corrects back to
    // the scale of the output under the rect.
    EXPECT_DOUBLE_EQ(frame.scale, screen->devicePixelRatio());
    EXPECT_EQ(frame.image.size(), QSize(qRound(400 * frame.scale), qRound(200 * frame.scale)));
    EXPECT_NE(frame.hash, 0U);
    qInfo() << "live CaptureArea 400x200:" << frame.grabMs << "ms, scale" << frame.scale << "image"
            << frame.image.size();
}

TEST(FrameSourceTest, reportsTheWorkspaceAsTheUnionOfEveryScreen)
{
    QRect expected;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (const QScreen *screen : screens) {
        expected |= screen->geometry();
    }
    EXPECT_EQ(maru::capture::workspaceRect(), expected);
    EXPECT_FALSE(expected.isEmpty());
}

int main(int argc, char **argv)
{
    // QGuiApplication rather than QCoreApplication: KWinFrameSource reads QScreen geometry to
    // correct an over-scaled reply, and the event loop is what delivers both the D-Bus reply and
    // the FakeFrameSource reply.
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
