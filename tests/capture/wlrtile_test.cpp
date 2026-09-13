// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The tile grid a zwlr_screencopy_v1 region grab snaps to.
//
// Hyprland retains one CScreenshareSession, and one event-loop timer, per distinct capture box
// for the life of the compositor (Hyprland 0.56.0,
// src/managers/screenshare/ScreenshareManager.cpp:122 and
// src/managers/screenshare/ScreenshareSession.cpp:87). The two properties the
// grid has to hold are therefore: the set of boxes a pointer sweeping an output produces is
// bounded, and the pointer keeps a margin from the tile's edge so the character under it and its
// paragraph are inside the grab.
#include "capture/scanregion.h"

#include <QRect>
#include <QSet>

#include <gtest/gtest.h>

using namespace maru::capture;

namespace
{

// The three ladder rungs of a 480x270 start, doubled twice, which is what
// capture::initialRect() and capture::grow() produce.
constexpr QSize kRung{480, 270};
constexpr QRect kOutput{0, 0, 3840, 2160};

} // namespace

TEST(WlrTileTest, answersAnEmptyRectForAnEmptyInput)
{
    EXPECT_TRUE(wlrTileFor(QRect{}, kOutput).isEmpty());
    EXPECT_TRUE(wlrTileFor(QRect{QPoint{0, 0}, kRung}, QRect{}).isEmpty());
}

TEST(WlrTileTest, keepsTheRequestedSize)
{
    const QRect tile = wlrTileFor(QRect{QPoint{1000, 700}, kRung}, kOutput);
    EXPECT_EQ(tile.size(), kRung);
}

TEST(WlrTileTest, staysInsideTheOutput)
{
    // Every corner and both edges of a 3840x2160 output.
    const QPoint corners[] = {{0, 0}, {3839, 0}, {0, 2159}, {3839, 2159}, {1920, 0}, {1920, 2159}};
    for (const QPoint corner : corners) {
        const QRect requested{
            corner.x() - kRung.width() / 2, corner.y() - kRung.height() / 2, kRung.width(), kRung.height()};
        const QRect tile = wlrTileFor(requested, kOutput);
        EXPECT_TRUE(kOutput.contains(tile))
            << "tile " << tile.x() << ',' << tile.y() << ' ' << tile.width() << 'x' << tile.height()
            << " left the output for the corner " << corner.x() << ',' << corner.y();
    }
}

TEST(WlrTileTest, clampsATileLargerThanTheOutput)
{
    const QRect small{0, 0, 320, 240};
    const QRect tile = wlrTileFor(QRect{-100, -100, 800, 600}, small);
    EXPECT_EQ(tile, small);
}

TEST(WlrTileTest, keepsThePointerAQuarterOfTheTileFromTheEdge)
{
    // The stride is half the tile, so the requested centre lands at most a quarter of the tile
    // from the tile's centre. Away from the output edges that is the whole story; a tile pushed
    // against an edge keeps the pointer inside, which is what the loop below asserts instead.
    const int marginX = kRung.width() / 4;
    const int marginY = kRung.height() / 4;
    for (int x = kRung.width(); x < kOutput.width() - kRung.width(); x += 7) {
        for (int y = kRung.height(); y < kOutput.height() - kRung.height(); y += 11) {
            const QPoint cursor{x, y};
            const QRect requested{x - kRung.width() / 2, y - kRung.height() / 2, kRung.width(), kRung.height()};
            const QRect tile = wlrTileFor(requested, kOutput);
            ASSERT_TRUE(tile.contains(cursor)) << "the pointer at " << x << ',' << y << " left its own tile";
            // The far edges are measured exclusively, as QRect::right() and QRect::bottom() are
            // the last pixel inside rather than the edge.
            EXPECT_GE(cursor.x() - tile.left(), marginX);
            EXPECT_GE(tile.right() + 1 - cursor.x(), marginX);
            EXPECT_GE(cursor.y() - tile.top(), marginY);
            EXPECT_GE(tile.bottom() + 1 - cursor.y(), marginY);
        }
    }
}

TEST(WlrTileTest, boundsTheNumberOfDistinctTiles)
{
    // The maximum number of distinct overlapping tiles:
    // (2 * width / tileWidth + 1) * (2 * height / tileHeight + 1).
    const int bound = ((2 * kOutput.width() / kRung.width()) + 1) * ((2 * kOutput.height() / kRung.height()) + 1);
    QSet<QPair<int, int>> tiles;
    for (int x = 0; x < kOutput.width(); x += 3) {
        for (int y = 0; y < kOutput.height(); y += 5) {
            const QRect requested{x - kRung.width() / 2, y - kRung.height() / 2, kRung.width(), kRung.height()};
            const QRect tile = wlrTileFor(requested, kOutput);
            tiles.insert({tile.x(), tile.y()});
        }
    }
    EXPECT_LE(tiles.size(), bound) << "a sweep of the output produced " << tiles.size() << " distinct capture boxes";
    // The grid is coarse enough to be worth having: an unsnapped rect would produce one box per
    // pointer position visited, which for this sweep is 1280 * 432.
    EXPECT_LT(tiles.size(), 1000);
}

TEST(WlrTileTest, isStableForNeighbouringPositions)
{
    // Two pointer positions one logical pixel apart in the middle of a tile answer the same tile,
    // which is the property that keeps the session count from growing with a slow pointer.
    const QRect first = wlrTileFor(QRect{1200 - 240, 800 - 135, 480, 270}, kOutput);
    const QRect second = wlrTileFor(QRect{1201 - 240, 800 - 135, 480, 270}, kOutput);
    EXPECT_EQ(first, second);
}

TEST(WlrTileTest, snapsRelativeToTheOutputOrigin)
{
    // A second output at x = 3840 produces the same tile offsets as the first, so a workspace
    // with several outputs has the same bound per output.
    const QRect right{3840, 0, 3840, 2160};
    const QRect onFirst = wlrTileFor(QRect{1000, 500, 480, 270}, kOutput);
    const QRect onSecond = wlrTileFor(QRect{3840 + 1000, 500, 480, 270}, right);
    EXPECT_EQ(onSecond.translated(-3840, 0), onFirst);
}
