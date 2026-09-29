// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// A grab under the offscreen platform plugin fails, because the screens of the plugin lack a
// monitor handle.
#include "capture/framesource.h"
#include "capture/winframesource.h"
#include "eventloop.h"
#include "livedesktop.h"
#include "win32/window.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QPainter>
#include <QScreen>
#include <QSignalSpy>
#include <QWidget>
#include <QWindow>

#include <cstdio>
#include <gtest/gtest.h>
#include <vector>

using namespace maru;
using capture::Frame;
using capture::WinFrameSource;

namespace
{

// A one-cell offset or a wrong scale in a grab changes the color a case reads.
class PatternWindow : public test::LiveWindow
{
public:
    explicit PatternWindow(QColor base)
        : m_base(base)
    {}

    void setBase(QColor base)
    {
        m_base = base;
        update();
    }

protected:
    void paintEvent(QPaintEvent * /*event*/) override
    {
        QPainter painter{this};
        for (int y = 0; y < height(); y += 16) {
            for (int x = 0; x < width(); x += 16) {
                painter.fillRect(QRect{x, y, 16, 16},
                                 QColor{(m_base.red() + x) % 256, (m_base.green() + y) % 256, m_base.blue()});
            }
        }
    }

private:
    QColor m_base;
};

QRect liveRect()
{
    QRect rect{0, 0, 320, 192};
    rect.moveCenter(QGuiApplication::primaryScreen()->geometry().center());
    return rect;
}

} // namespace

TEST(WinFrameSourceMapping, isTheIdentityOnAnUnscaledScreen)
{
    const QRect screen{0, 0, 2560, 1440};
    EXPECT_EQ(WinFrameSource::nativeRect(QRect{100, 50, 640, 400}, screen, screen), (QRect{100, 50, 640, 400}));
}

TEST(WinFrameSourceMapping, scalesFromTheOriginOfTheScreen)
{
    // Qt places the top-left corner of the logical geometry at the device coordinates and divides
    // the size by the scale factor.
    const QRect logical{2560, 153, 1280, 720};
    const QRect native{2560, 153, 1920, 1080};
    EXPECT_EQ(WinFrameSource::nativeRect(QRect{2660, 253, 200, 100}, logical, native), (QRect{2710, 303, 300, 150}));
}

TEST(WinFrameSourceMapping, scalesOnAScreenLeftOfThePrimary)
{
    const QRect logical{-2048, 0, 2048, 1152};
    const QRect native{-2560, 0, 2560, 1440};
    EXPECT_EQ(WinFrameSource::nativeRect(QRect{-1000, 100, 400, 200}, logical, native), (QRect{-1250, 125, 500, 250}));
}

TEST(WinFrameSourceMapping, keepsAdjacentRectanglesAdjacent)
{
    const QRect logical{0, 0, 2048, 1152};
    const QRect native{0, 0, 2560, 1440};
    for (int x = 0; x < 40; ++x) {
        const QRect left = WinFrameSource::nativeRect(QRect{0, 0, x + 1, 10}, logical, native);
        const QRect right = WinFrameSource::nativeRect(QRect{x + 1, 0, 7, 10}, logical, native);
        EXPECT_EQ(left.right() + 1, right.left()) << "split at logical x " << x + 1;
    }
}

TEST(WinFrameSourceMapping, clipsToTheScreen)
{
    const QRect screen{0, 0, 1920, 1080};
    EXPECT_EQ(WinFrameSource::nativeRect(QRect{1800, 1000, 300, 200}, screen, screen), (QRect{1800, 1000, 120, 80}));
    EXPECT_TRUE(WinFrameSource::nativeRect(QRect{0, 0, 10, 10}, QRect{}, screen).isEmpty());
}

TEST(WinFrameSourceQuantize, clipsToTheScreenUnderTheCentre)
{
    WinFrameSource source;
    const QRect screen = QGuiApplication::primaryScreen()->geometry();
    const QRect overhanging{screen.right() - 199, screen.top() + 10, 300, 100};
    EXPECT_EQ(source.quantize(overhanging), (QRect{screen.right() - 199, screen.top() + 10, 200, 100}));
    EXPECT_EQ(source.capturesOwnWindows(), !win32::captureExclusionSupported());
}

TEST(WinFrameSourceOffscreen, failsForAScreenWithNoMonitorAndKeepsTheLatestRequest)
{
    if (test::liveDesktop()) {
        GTEST_SKIP() << "unset MARUPOP_LIVE_DESKTOP to run the offscreen case; the windows platform plugin gives "
                        "every screen a monitor handle";
    }
    WinFrameSource source;
    QSignalSpy failed{&source, &capture::FrameSource::failed};
    // Latest wins: a request made while a grab is in flight replaces the pending request.
    source.grab(QRect{0, 0, 10, 10});
    source.grab(QRect{10, 0, 10, 10});
    source.grab(QRect{20, 0, 10, 10});
    ASSERT_TRUE(test::waitFor([&] {
        return failed.size() >= 2;
    }));
    test::pumpFor(50);
    EXPECT_EQ(failed.size(), 2);
}

class WinFrameSourceLive : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!test::liveDesktop()) {
            GTEST_SKIP() << test::liveDesktopSkip;
        }
        pattern.setGeometry(liveRect());
        pattern.show();
        ASSERT_TRUE(test::waitFor([&] {
            return pattern.isVisible() && pattern.windowHandle() != nullptr && pattern.windowHandle()->isExposed();
        }));
        // 50 ms, three refresh intervals at 60 Hz, for DWM to compose the shown window.
        test::pumpFor(50);
        // WinFrameSource falls back to BitBlt for every grab in a Remote Desktop session and on a
        // display adapter without DXGI Desktop Duplication support.
        source.setDesktopDuplicationEnabled(true);
        duplicationAvailable = test::grabFrame(source, liveRect()).has_value() &&
                               source.lastMethod() == WinFrameSource::Method::DesktopDuplication;
        if (!duplicationAvailable) {
            std::printf("DXGI Desktop Duplication is unavailable on this desktop; its cases compare BitBlt alone\n");
        }
    }

    [[nodiscard]] std::vector<bool> paths() const
    {
        return duplicationAvailable ? std::vector<bool>{true, false} : std::vector<bool>{false};
    }

    std::optional<Frame> grabWith(bool duplication, const QRect &rect)
    {
        source.setDesktopDuplicationEnabled(duplication);
        return test::grabFrame(source, rect);
    }

    PatternWindow pattern{QColor{40, 80, 120}};
    WinFrameSource source;
    bool duplicationAvailable = false;
};

TEST_F(WinFrameSourceLive, bothPathsReturnTheDevicePixelsOfTheRegion)
{
    const QRect rect = liveRect();
    const QScreen *screen = QGuiApplication::screenAt(rect.center());
    ASSERT_NE(screen, nullptr);
    const QRect native = WinFrameSource::nativeRect(rect, screen->geometry(), WinFrameSource::nativeGeometry(screen));
    for (const bool duplication : paths()) {
        const std::optional<Frame> frame = grabWith(duplication, rect);
        ASSERT_TRUE(frame.has_value()) << (duplication ? "DXGI" : "BitBlt");
        EXPECT_EQ(frame->image.size(), native.size());
        EXPECT_EQ(frame->logicalRect, rect);
        EXPECT_DOUBLE_EQ(frame->scale, static_cast<double>(native.width()) / rect.width());
        EXPECT_EQ(frame->image.format(), QImage::Format_RGB32);
        EXPECT_EQ(source.lastMethod(),
                  duplication ? WinFrameSource::Method::DesktopDuplication : WinFrameSource::Method::BitBlt);
        const QColor cell = frame->image.pixelColor(qRound(40 * frame->scale), qRound(56 * frame->scale));
        EXPECT_EQ(cell, (QColor{(40 + 32) % 256, (80 + 48) % 256, 120})) << (duplication ? "DXGI" : "BitBlt");
    }
}

TEST_F(WinFrameSourceLive, bothPathsReturnTheSamePixels)
{
    if (!duplicationAvailable) {
        GTEST_SKIP() << "DXGI Desktop Duplication is unavailable on this desktop, so there is one path to compare";
    }
    const QRect rect = liveRect();
    const std::optional<Frame> duplicated = grabWith(true, rect);
    const std::optional<Frame> blitted = grabWith(false, rect);
    ASSERT_TRUE(duplicated.has_value());
    ASSERT_TRUE(blitted.has_value());
    // Equal hashes let ScanCache answer a grab from one path with the recognition of the other
    // path.
    EXPECT_EQ(duplicated->hash, blitted->hash);
    EXPECT_EQ(duplicated->image, blitted->image);
}

TEST_F(WinFrameSourceLive, neitherPathCapturesAnExcludedWindow)
{
    PatternWindow cover{QColor{255, 0, 255}};
    QRect coverRect = liveRect();
    coverRect.setSize(coverRect.size() / 2);
    cover.setGeometry(coverRect);
    cover.winId();
    ASSERT_TRUE(win32::excludeFromCapture(cover.windowHandle()));
    cover.show();
    ASSERT_TRUE(test::waitFor([&] {
        return cover.windowHandle()->isExposed();
    }));
    test::pumpFor(50);
    for (const bool duplication : paths()) {
        const std::optional<Frame> frame = grabWith(duplication, liveRect());
        ASSERT_TRUE(frame.has_value());
        const QColor cell = frame->image.pixelColor(qRound(8 * frame->scale), qRound(8 * frame->scale));
        EXPECT_EQ(cell, (QColor{40, 80, 120})) << (duplication ? "DXGI" : "BitBlt");
    }
}

TEST_F(WinFrameSourceLive, showsAChangedWindowWithinTheFreshnessBound)
{
    // A repaint reaches a BitBlt grab with the next composed frame and a DXGI grab one frame later.
    // 100 ms is six refresh intervals at 60 Hz.
    const QRect rect = liveRect();
    for (const bool duplication : paths()) {
        source.setDesktopDuplicationEnabled(duplication);
        (void)test::grabFrame(source, rect);
        const QColor next = duplication ? QColor{200, 30, 60} : QColor{10, 220, 90};
        pattern.setBase(next);
        pattern.repaint();
        QElapsedTimer timer;
        timer.start();
        bool seen = false;
        while (timer.elapsed() < 500 && !seen) {
            const std::optional<Frame> frame = test::grabFrame(source, rect);
            seen = frame.has_value() && frame->image.pixelColor(2, 2) == next;
        }
        std::printf(
            "%s freshness: %lld ms\n", duplication ? "DXGI" : "BitBlt", static_cast<long long>(timer.elapsed()));
        EXPECT_TRUE(seen);
        EXPECT_LT(timer.elapsed(), 100) << (duplication ? "DXGI" : "BitBlt");
    }
}

TEST_F(WinFrameSourceLive, grabsWithinTheLatencyBounds)
{
    // DXGI Desktop Duplication copies the region from an output copy in video memory. BitBlt waits
    // for the next composition.
    const QRect rect = liveRect();
    for (const bool duplication : paths()) {
        source.setDesktopDuplicationEnabled(duplication);
        (void)test::grabFrame(source, rect);
        std::vector<double> samples;
        for (int i = 0; i < 60; ++i) {
            QElapsedTimer timer;
            timer.start();
            ASSERT_TRUE(test::grabFrame(source, rect).has_value());
            samples.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
        }
        std::printf("%s grab of %dx%d: p50 %.3f ms, p90 %.3f ms\n",
                    duplication ? "DXGI" : "BitBlt",
                    rect.width(),
                    rect.height(),
                    test::percentile(samples, 0.5),
                    test::percentile(samples, 0.9));
        if (duplication) {
            EXPECT_LT(test::percentile(samples, 0.5), 4.0);
        } else {
            EXPECT_LT(test::percentile(samples, 0.5), 40.0);
        }
    }
}

TEST_F(WinFrameSourceLive, releasesAndRecreatesTheDuplication)
{
    if (!duplicationAvailable) {
        GTEST_SKIP() << "DXGI Desktop Duplication is unavailable on this desktop";
    }
    const QRect rect = liveRect();
    ASSERT_TRUE(grabWith(true, rect).has_value());
    source.release();
    const std::optional<Frame> again = grabWith(true, rect);
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(source.lastMethod(), WinFrameSource::Method::DesktopDuplication);
}

int main(int argc, char **argv)
{
    test::selectLivePlatform();
    QApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
