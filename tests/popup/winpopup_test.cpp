// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The reposition bound is a p90 under the 8 ms pointer pump interval.
#include "capture/framesource.h"
#include "capture/winframesource.h"
#include "core/settings.h"
#include "eventloop.h"
#include "livedesktop.h"
#include "popup/entrymodel.h"
#include "popup/popupwindow.h"
#include "win32/window.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QPainter>
#include <QScreen>
#include <QWindow>

#include <cstdio>
#include <gtest/gtest.h>
#include <vector>
#include <windows.h>

using namespace maru;
using namespace maru::popup;

namespace
{

PopupModel shortModel()
{
    PopupModel model;
    Entry entry;
    entry.headword = QStringLiteral("見出し語");
    entry.readings = QStringList{QStringLiteral("みだしご")};
    Sense sense;
    sense.glosses = QStringList{QStringLiteral("headword")};
    entry.senses.append(sense);
    model.entries.append(entry);
    return model;
}

class Backdrop : public test::LiveWindow
{
protected:
    void paintEvent(QPaintEvent * /*event*/) override
    {
        QPainter{this}.fillRect(rect(), QColor{12, 34, 56});
    }
};

LONG_PTR extendedStyle(const QWidget &widget)
{
    return GetWindowLongPtrW(static_cast<HWND>(win32::handleOf(widget.windowHandle())), GWL_EXSTYLE);
}

DWORD affinity(const QWidget &widget)
{
    DWORD value = WDA_NONE;
    GetWindowDisplayAffinity(static_cast<HWND>(win32::handleOf(widget.windowHandle())), &value);
    return value;
}

class WinPopupLive : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!test::liveDesktop()) {
            GTEST_SKIP() << test::liveDesktopSkip;
        }
        PopSettings::self()->setDefaults();
        // A 0 ms fade shows the card at full opacity on the first frame.
        PopSettings::setPopupFadeMs(0);
        PopSettings::self()->save();
        screen = QGuiApplication::primaryScreen();
        center = screen->geometry().center();
    }

    void showCard(PopupWindow &card)
    {
        card.applyTheme();
        card.setModel(shortModel());
        card.showNear(center, screen);
        ASSERT_TRUE(test::waitFor([&] {
            return card.isVisible() && card.windowHandle() != nullptr && card.windowHandle()->isExposed();
        }));
    }

    QScreen *screen = nullptr;
    QPoint center;
};

} // namespace

TEST_F(WinPopupLive, isATopmostToolWindowThePointerPassesThrough)
{
    PopupWindow card;
    showCard(card);
    const LONG_PTR style = extendedStyle(card);
    EXPECT_NE(style & WS_EX_TOPMOST, 0);
    EXPECT_NE(style & WS_EX_TOOLWINDOW, 0);
    EXPECT_NE(style & WS_EX_NOACTIVATE, 0);
    EXPECT_NE(style & WS_EX_TRANSPARENT, 0);
}

TEST_F(WinPopupLive, isExcludedFromCapture)
{
    PopupWindow card;
    showCard(card);
    EXPECT_EQ(affinity(card), static_cast<DWORD>(WDA_EXCLUDEFROMCAPTURE));
}

TEST_F(WinPopupLive, leavesTheDesktopUnderItToTheGrab)
{
    PopupWindow card;
    card.applyTheme();
    card.setModel(shortModel());
    card.showNear(center, screen);
    ASSERT_TRUE(test::waitFor([&] {
        return card.isVisible();
    }));
    Backdrop backdrop;
    backdrop.setGeometry(card.popupRect().adjusted(-20, -20, 20, 20));
    backdrop.show();
    // A hide and a show raise the card above the backdrop, as a new lookup result does.
    card.hidePopup();
    test::waitFor([&] {
        return !card.isVisible();
    });
    card.showNear(center, screen);
    test::pumpFor(80);

    capture::WinFrameSource source;
    for (const bool duplication : {true, false}) {
        source.setDesktopDuplicationEnabled(duplication);
        const std::optional<capture::Frame> frame = test::grabFrame(source, card.popupRect());
        ASSERT_TRUE(frame.has_value());
        const QImage &image = frame->image;
        EXPECT_EQ(image.pixelColor(image.width() / 2, image.height() / 2), (QColor{12, 34, 56}))
            << (duplication ? "DXGI" : "BitBlt") << " read the card instead of the desktop under it";
    }
}

TEST_F(WinPopupLive, showingAndMovingLeaveTheForegroundAlone)
{
    const HWND before = GetForegroundWindow();
    PopupWindow card;
    showCard(card);
    for (int step = 0; step < 20; ++step) {
        card.showNear(center + QPoint{step * 3, step}, screen);
    }
    test::pumpFor(20);
    EXPECT_EQ(GetForegroundWindow(), before);
}

TEST_F(WinPopupLive, repositionsWithinThePointerPump)
{
    PopupWindow card;
    showCard(card);
    constexpr int kMoves = 250;
    std::vector<double> costs;
    costs.reserve(kMoves);
    for (int step = 0; step < kMoves; ++step) {
        const QPoint point = center + QPoint{(step * 4) % 200, (step * 3) % 150};
        QElapsedTimer timer;
        timer.start();
        card.showNear(point, screen);
        costs.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
    }
    const double p50 = test::percentile(costs, 0.5);
    const double p90 = test::percentile(costs, 0.9);
    std::printf("popup reposition over %d moves: p50 %.3f ms, p90 %.3f ms, max %.3f ms\n",
                kMoves,
                p50,
                p90,
                test::percentile(costs, 1.0));
    EXPECT_LT(p90, 8.0);
}

TEST_F(WinPopupLive, pinningRecreatesTheWindowWithTheExclusion)
{
    PopupWindow card;
    showCard(card);

    card.setPinned(true);
    ASSERT_TRUE(test::waitFor([&] {
        return card.isVisible();
    }));
    EXPECT_EQ(affinity(card), static_cast<DWORD>(WDA_EXCLUDEFROMCAPTURE));
    EXPECT_EQ(extendedStyle(card) & WS_EX_TRANSPARENT, 0) << "a pinned card takes the pointer";
    EXPECT_EQ(extendedStyle(card) & WS_EX_NOACTIVATE, 0) << "a pinned card takes the keyboard";

    card.setPinned(false);
    ASSERT_TRUE(test::waitFor([&] {
        return card.isVisible();
    }));
    EXPECT_NE(extendedStyle(card) & WS_EX_TRANSPARENT, 0);
    EXPECT_EQ(affinity(card), static_cast<DWORD>(WDA_EXCLUDEFROMCAPTURE));
}

int main(int argc, char **argv)
{
    test::selectLivePlatform();
    QApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
