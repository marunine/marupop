// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The progressive scan rect. Every case here runs against a workspace given as a literal, so
// the suite is independent of the outputs the machine running it has.
#include "capture/framesource.h"
#include "capture/scanregion.h"

#include <QPoint>
#include <QRect>
#include <QSize>

#include <gtest/gtest.h>

using namespace maru::capture;

namespace
{

// One 2560x1440 output at the origin.
constexpr QRect kSingle{0, 0, 2560, 1440};
// A three-output layout: two 2560x1440 screens followed by a 1920x1080 screen.
// The differing final size exercises workspace bounds without assuming equal outputs.
constexpr QRect kTriple{0, 0, 7040, 1440};
constexpr QSize kInitial{480, 270};
constexpr QSize kMaximum{1600, 900};

} // namespace

TEST(ScanRegionTest, centresTheInitialRectOnTheCursor)
{
    const QRect rect = initialRect({1000, 500}, kInitial, kSingle);
    EXPECT_EQ(rect.size(), kInitial);
    EXPECT_TRUE(rect.contains(QPoint(1000, 500)));
    // QRect::moveCenter() places the centre at topLeft + (size - 1) / 2, so the rect starts one
    // pixel right of and below an exact halving.
    EXPECT_EQ(rect, QRect(761, 366, 480, 270));
    EXPECT_TRUE(kSingle.contains(rect));
}

TEST(ScanRegionTest, movesTheInitialRectInsideTheWorkspaceAtEachEdge)
{
    // Top left corner.
    EXPECT_EQ(initialRect({10, 10}, kInitial, kSingle), QRect(0, 0, 480, 270));
    // Bottom right corner.
    EXPECT_EQ(initialRect({2550, 1430}, kInitial, kSingle), QRect(2080, 1170, 480, 270));
    // Left edge alone.
    EXPECT_EQ(initialRect({100, 700}, kInitial, kSingle), QRect(0, 566, 480, 270));
    // Top edge alone.
    EXPECT_EQ(initialRect({1000, 50}, kInitial, kSingle), QRect(761, 0, 480, 270));
    // Right edge alone.
    EXPECT_EQ(initialRect({2500, 700}, kInitial, kSingle), QRect(2080, 566, 480, 270));
    // Bottom edge alone.
    EXPECT_EQ(initialRect({1000, 1400}, kInitial, kSingle), QRect(761, 1170, 480, 270));
}

TEST(ScanRegionTest, clampsARectLargerThanTheWorkspace)
{
    const QRect rect = initialRect({1000, 500}, QSize(4000, 3000), kSingle);
    EXPECT_EQ(rect, kSingle);
}

TEST(ScanRegionTest, spansTheWholeWorkspaceOnAThreeOutputDesktop)
{
    // A cursor on the third output, 1920x1080 at x 5120.
    const QRect rect = initialRect({5200, 100}, kInitial, kTriple);
    EXPECT_EQ(rect, QRect(4961, 0, 480, 270));
    EXPECT_TRUE(kTriple.contains(rect));
    // A cursor at the right edge of the workspace stays inside it.
    const QRect edge = initialRect({7039, 1439}, kInitial, kTriple);
    EXPECT_EQ(edge, QRect(6560, 1170, 480, 270));
    EXPECT_TRUE(kTriple.contains(edge));
}

TEST(ScanRegionTest, returnsAnEmptyRectForAnEmptyInput)
{
    EXPECT_TRUE(initialRect({100, 100}, QSize(0, 0), kSingle).isEmpty());
    EXPECT_TRUE(initialRect({100, 100}, kInitial, QRect()).isEmpty());
}

TEST(ScanRegionTest, doublesEachDimensionUpToTheMaximum)
{
    const QPoint cursor{1000, 500};
    const QRect first = initialRect(cursor, kInitial, kSingle);
    ASSERT_EQ(first.size(), QSize(480, 270));

    const std::optional<QRect> second = grow(first, cursor, kMaximum, kSingle);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->size(), QSize(960, 540));
    EXPECT_TRUE(second->contains(first));

    const std::optional<QRect> third = grow(*second, cursor, kMaximum, kSingle);
    ASSERT_TRUE(third.has_value());
    EXPECT_EQ(third->size(), QSize(1600, 900));
    EXPECT_TRUE(third->contains(*second));

    // 1600x900 is the maximum, so the ladder is exhausted.
    EXPECT_FALSE(grow(*third, cursor, kMaximum, kSingle).has_value());
}

TEST(ScanRegionTest, keepsTheCursorInsideAGrownRect)
{
    // The pointer moved 1400 px right and 700 px down between the two steps.
    const QRect first = initialRect({100, 100}, kInitial, kSingle);
    const QPoint moved{1500, 800};
    const std::optional<QRect> second = grow(first, moved, kMaximum, kSingle);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->size(), QSize(960, 540));
    EXPECT_TRUE(second->contains(moved));
    EXPECT_TRUE(kSingle.contains(*second));
}

TEST(ScanRegionTest, capsTheGrowthAtTheWorkspace)
{
    // A workspace smaller than the configured maximum in both dimensions.
    const QRect small{0, 0, 800, 600};
    const QRect first = initialRect({400, 300}, QSize(400, 300), small);
    const std::optional<QRect> second = grow(first, {400, 300}, kMaximum, small);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*second, small);
    EXPECT_FALSE(grow(*second, {400, 300}, kMaximum, small).has_value());
}

TEST(ScanRegionTest, growsOnlyOneDimensionOnceTheOtherIsCapped)
{
    const QRect wide{0, 0, 1600, 200};
    const std::optional<QRect> next = grow(wide, {800, 100}, kMaximum, kSingle);
    ASSERT_TRUE(next.has_value());
    EXPECT_EQ(next->size(), QSize(1600, 400));
}

TEST(ScanRegionTest, refusesToGrowWithoutAWorkspace)
{
    EXPECT_FALSE(grow(QRect(0, 0, 480, 270), {100, 100}, kMaximum, QRect()).has_value());
    EXPECT_FALSE(grow(QRect(0, 0, 480, 270), {100, 100}, QSize(), kSingle).has_value());
}

TEST(ScanRegionTest, reportsALineBoxAtTheFrameEdge)
{
    const QRect frame{100, 100, 400, 200};
    // 8 px from the left edge, inside a 10 px margin.
    EXPECT_TRUE(touchesEdge(QRect(108, 150, 50, 20), frame, 10));
    // 8 px from the right edge.
    EXPECT_TRUE(touchesEdge(QRect(300, 150, 192, 20), frame, 10));
    // 5 px from the top edge.
    EXPECT_TRUE(touchesEdge(QRect(200, 105, 50, 20), frame, 10));
    // 4 px from the bottom edge: bottom 295, frame bottom 299.
    EXPECT_TRUE(touchesEdge(QRect(200, 250, 50, 46), frame, 10));
    // Clear of every edge by more than the margin.
    EXPECT_FALSE(touchesEdge(QRect(200, 150, 50, 20), frame, 10));
    // A zero margin reports only a box that reaches the edge exactly.
    EXPECT_FALSE(touchesEdge(QRect(101, 150, 50, 20), frame, 0));
    EXPECT_TRUE(touchesEdge(QRect(100, 150, 50, 20), frame, 0));
    // Empty inputs report false rather than touching every edge.
    EXPECT_FALSE(touchesEdge(QRect(), frame, 10));
    EXPECT_FALSE(touchesEdge(QRect(200, 150, 50, 20), QRect(), 10));
}

TEST(ScanRegionTest, mapsBetweenFramePixelsAndLogicalCoordinates)
{
    Frame frame;
    frame.logicalRect = QRect(1000, 500, 400, 200);
    frame.scale = 2.0;

    EXPECT_EQ(imageToLogical(frame, QPoint(0, 0)), QPoint(1000, 500));
    EXPECT_EQ(imageToLogical(frame, QPoint(200, 100)), QPoint(1100, 550));
    EXPECT_EQ(logicalToImage(frame, QPoint(1100, 550)), QPoint(200, 100));
    EXPECT_EQ(logicalToImage(frame, QPoint(1000, 500)), QPoint(0, 0));

    EXPECT_EQ(imageToLogical(frame, QRect(100, 60, 80, 40)), QRect(1050, 530, 40, 20));
    EXPECT_EQ(logicalToImage(frame, QRect(1050, 530, 40, 20)), QRect(100, 60, 80, 40));

    // A scale of 1 leaves the offset alone.
    Frame unscaled;
    unscaled.logicalRect = QRect(0, 0, 400, 200);
    unscaled.scale = 1.0;
    EXPECT_EQ(imageToLogical(unscaled, QPoint(17, 23)), QPoint(17, 23));
    EXPECT_EQ(logicalToImage(unscaled, QPoint(17, 23)), QPoint(17, 23));
}
