// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The four placement modes of popup::placePopup(). Every case states the screen rectangle, so
// the multi-monitor cases with a non-zero origin read against the same arithmetic as the
// single-screen ones.
#include "popup/placement.h"

#include <gtest/gtest.h>

using namespace maru;
using namespace maru::popup;

namespace
{

// A 1920 by 1080 primary screen at the origin, and the same panel placed to its right and to
// its left, which is what a two-head layout produces.
const QRect primaryScreen{0, 0, 1920, 1080};
const QRect rightScreen{1920, 0, 1920, 1080};
const QRect leftScreen{-1920, -200, 1920, 1080};

constexpr int offset = 15;
const QSize card{200, 100};

} // namespace

TEST(PlacementTest, flipBothPrefersBelowRightOfThePointer)
{
    const QRect placed = placePopup(QPoint{100, 100}, card, primaryScreen, PopupPositionMode::FlipBoth, offset);
    EXPECT_EQ(placed, QRect(115, 115, 200, 100));
}

TEST(PlacementTest, flipBothFlipsEachAxisIndependently)
{
    // The right edge alone.
    EXPECT_EQ(placePopup(QPoint{1900, 100}, card, primaryScreen, PopupPositionMode::FlipBoth, offset).topLeft(),
              QPoint(1685, 115));
    // The bottom edge alone.
    EXPECT_EQ(placePopup(QPoint{100, 1050}, card, primaryScreen, PopupPositionMode::FlipBoth, offset).topLeft(),
              QPoint(115, 935));
    // Both edges at once.
    EXPECT_EQ(placePopup(QPoint{1900, 1050}, card, primaryScreen, PopupPositionMode::FlipBoth, offset).topLeft(),
              QPoint(1685, 935));
}

TEST(PlacementTest, flipHorizontallyPushesTheVerticalPositionBackIn)
{
    // X flips at the right edge.
    EXPECT_EQ(placePopup(QPoint{1900, 100}, card, primaryScreen, PopupPositionMode::FlipHorizontally, offset).topLeft(),
              QPoint(1685, 115));
    // Y is clamped rather than flipped, so the card ends flush with the bottom edge.
    EXPECT_EQ(placePopup(QPoint{100, 1050}, card, primaryScreen, PopupPositionMode::FlipHorizontally, offset).topLeft(),
              QPoint(115, 980));
}

TEST(PlacementTest, flipVerticallyPushesTheHorizontalPositionBackIn)
{
    EXPECT_EQ(placePopup(QPoint{100, 1050}, card, primaryScreen, PopupPositionMode::FlipVertically, offset).topLeft(),
              QPoint(115, 935));
    EXPECT_EQ(placePopup(QPoint{1900, 100}, card, primaryScreen, PopupPositionMode::FlipVertically, offset).topLeft(),
              QPoint(1720, 115));
}

TEST(PlacementTest, visualNovelPlacesByVerticalThirds)
{
    const auto placeAt = [](int y) {
        return placePopup(QPoint{960, y}, card, primaryScreen, PopupPositionMode::VisualNovel, offset).y();
    };
    // Upper third: below the pointer.
    EXPECT_EQ(placeAt(100), 115);
    // Lower third: above the pointer.
    EXPECT_EQ(placeAt(1000), 885);
    // Middle third above the half: below the pointer.
    EXPECT_EQ(placeAt(400), 415);
    // Middle third below the half: above the pointer.
    EXPECT_EQ(placeAt(600), 485);
}

TEST(PlacementTest, visualNovelInterpolatesTheHorizontalAnchor)
{
    const auto placeAt = [](int x) {
        return placePopup(QPoint{x, 100}, card, primaryScreen, PopupPositionMode::VisualNovel, offset).x();
    };
    // Left edge: anchored right of the pointer.
    EXPECT_EQ(placeAt(0), 15);
    // Half: centered on the pointer.
    EXPECT_EQ(placeAt(960), 860);
    // Right edge: anchored left of the pointer.
    EXPECT_EQ(placeAt(1920), 1705);
    // A quarter across: between the right anchor and the centre, and monotonic.
    const int quarter = placeAt(480);
    EXPECT_GT(quarter, 380);
    EXPECT_LT(quarter, 495);
}

TEST(PlacementTest, clampsIntoTheScreenAfterEveryMode)
{
    // A pointer past the right edge would put the card off-screen under every mode.
    for (const PopupPositionMode mode : {PopupPositionMode::FlipBoth,
                                         PopupPositionMode::FlipVertically,
                                         PopupPositionMode::FlipHorizontally,
                                         PopupPositionMode::VisualNovel}) {
        const QRect placed = placePopup(QPoint{1919, 1079}, card, primaryScreen, mode, offset);
        EXPECT_GE(placed.left(), primaryScreen.left());
        EXPECT_GE(placed.top(), primaryScreen.top());
        EXPECT_LE(placed.left() + placed.width(), primaryScreen.x() + primaryScreen.width());
        EXPECT_LE(placed.top() + placed.height(), primaryScreen.y() + primaryScreen.height());
    }
}

TEST(PlacementTest, anchorsACardLargerThanTheScreenAtItsOrigin)
{
    const QSize oversized{3000, 2000};
    const QRect placed = placePopup(QPoint{100, 100}, oversized, primaryScreen, PopupPositionMode::FlipBoth, offset);
    EXPECT_EQ(placed.topLeft(), primaryScreen.topLeft());
    EXPECT_EQ(placed.size(), oversized);
}

TEST(PlacementTest, worksAgainstAScreenWithANonZeroOrigin)
{
    // The right-hand panel: the arithmetic repeats with 1920 added to every x.
    EXPECT_EQ(placePopup(QPoint{2000, 100}, card, rightScreen, PopupPositionMode::FlipBoth, offset).topLeft(),
              QPoint(2015, 115));
    EXPECT_EQ(placePopup(QPoint{3830, 100}, card, rightScreen, PopupPositionMode::FlipBoth, offset).topLeft(),
              QPoint(3615, 115));
    // The vertical thirds of the right-hand panel are measured from its own top edge.
    EXPECT_EQ(placePopup(QPoint{2880, 1000}, card, rightScreen, PopupPositionMode::VisualNovel, offset).y(), 885);
    // The half of the right-hand panel centres the card on the pointer.
    EXPECT_EQ(placePopup(QPoint{2880, 100}, card, rightScreen, PopupPositionMode::VisualNovel, offset).x(), 2780);
}

TEST(PlacementTest, worksAgainstAScreenWithANegativeOrigin)
{
    // The left-hand panel sits at x -1920 and y -200.
    EXPECT_EQ(placePopup(QPoint{-1900, -180}, card, leftScreen, PopupPositionMode::FlipBoth, offset).topLeft(),
              QPoint(-1885, -165));
    // Its lower third starts at y 520, so a pointer at 700 places the card above.
    EXPECT_EQ(placePopup(QPoint{-960, 700}, card, leftScreen, PopupPositionMode::VisualNovel, offset).y(), 585);
    // Its bottom edge is y 880.
    const QRect clamped = placePopup(QPoint{-960, 879}, card, leftScreen, PopupPositionMode::FlipVertically, offset);
    EXPECT_LE(clamped.top() + clamped.height(), leftScreen.y() + leftScreen.height());
}

// popup::placePopupAvoiding(), which is what keeps the card off the paragraph it answers for on a
// session whose pixel source composites MaruPop's own windows into the next grab.

TEST(PlacementAvoidingTest, answersThePlainPlacementForAnEmptyAvoidRect)
{
    for (const PopupPositionMode mode : {PopupPositionMode::VisualNovel,
                                         PopupPositionMode::FlipHorizontally,
                                         PopupPositionMode::FlipVertically,
                                         PopupPositionMode::FlipBoth}) {
        EXPECT_EQ(placePopupAvoiding(QPoint{500, 400}, card, primaryScreen, mode, offset, QRect{}),
                  placePopup(QPoint{500, 400}, card, primaryScreen, mode, offset));
    }
}

TEST(PlacementAvoidingTest, answersThePlainPlacementWhereItAlreadyMissesTheText)
{
    // A paragraph in the top left corner, and a pointer in the bottom right: the card the mode
    // chooses is nowhere near it.
    const QRect paragraph{0, 0, 300, 40};
    EXPECT_EQ(
        placePopupAvoiding(QPoint{1500, 900}, card, primaryScreen, PopupPositionMode::FlipBoth, offset, paragraph),
        placePopup(QPoint{1500, 900}, card, primaryScreen, PopupPositionMode::FlipBoth, offset));
}

TEST(PlacementAvoidingTest, movesTheCardOffTheLineItAnswersFor)
{
    // A horizontal line of text through the pointer. FlipHorizontally puts the card right of the
    // pointer at the same y, which is exactly over the rest of the line.
    const QRect paragraph{400, 380, 900, 44};
    const QPoint cursor{600, 400};
    const QRect plain = placePopup(cursor, card, primaryScreen, PopupPositionMode::FlipHorizontally, offset);
    ASSERT_TRUE(plain.intersects(paragraph)) << "the case no longer exercises the overlap it was written for";

    const QRect avoided =
        placePopupAvoiding(cursor, card, primaryScreen, PopupPositionMode::FlipHorizontally, offset, paragraph);
    EXPECT_FALSE(avoided.intersects(paragraph));
    EXPECT_TRUE(primaryScreen.contains(avoided));
    EXPECT_EQ(avoided.size(), card);
}

TEST(PlacementAvoidingTest, takesTheNearestBand)
{
    // The paragraph is a band across the screen with 336 logical pixels free above it and 636
    // below. The card fits in both; the one nearest the unconstrained placement wins, and the
    // unconstrained placement for FlipVertically is below the pointer.
    const QRect paragraph{0, 336, 1920, 108};
    const QPoint cursor{960, 380};
    const QRect avoided =
        placePopupAvoiding(cursor, card, primaryScreen, PopupPositionMode::FlipVertically, offset, paragraph);
    EXPECT_FALSE(avoided.intersects(paragraph));
    EXPECT_GT(avoided.top(), paragraph.bottom()) << "the card went above the text when below was nearer";
}

TEST(PlacementAvoidingTest, keepsThePlainPlacementWhereNoBandFitsTheCard)
{
    // A paragraph that leaves no strip of the screen wide or tall enough for the card. A card
    // outside the screen is worse than a card over the text, so the plain placement stands.
    const QRect paragraph{50, 50, 1820, 980};
    const QPoint cursor{960, 540};
    EXPECT_EQ(placePopupAvoiding(cursor, card, primaryScreen, PopupPositionMode::FlipBoth, offset, paragraph),
              placePopup(cursor, card, primaryScreen, PopupPositionMode::FlipBoth, offset));
}

TEST(PlacementAvoidingTest, worksAgainstAScreenWithANonZeroOrigin)
{
    const QRect paragraph{2200, 380, 900, 44};
    const QPoint cursor{2400, 400};
    const QRect avoided =
        placePopupAvoiding(cursor, card, rightScreen, PopupPositionMode::FlipHorizontally, offset, paragraph);
    EXPECT_FALSE(avoided.intersects(paragraph));
    EXPECT_TRUE(rightScreen.contains(avoided));
}

TEST(PlacementAvoidingTest, clipsTheAvoidRectToTheScreen)
{
    // The paragraph runs off the left edge of the right-hand panel, which is where a scan region
    // that spanned two outputs puts it. Only the part on this screen constrains the card.
    const QRect paragraph{1000, 380, 1400, 44};
    const QPoint cursor{2400, 400};
    const QRect avoided =
        placePopupAvoiding(cursor, card, rightScreen, PopupPositionMode::FlipHorizontally, offset, paragraph);
    EXPECT_FALSE(avoided.intersects(paragraph.intersected(rightScreen)));
    EXPECT_TRUE(rightScreen.contains(avoided));
}
