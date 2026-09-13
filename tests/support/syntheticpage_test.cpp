// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// SyntheticPage geometry is shared by all hit-test suites. Cover empty line lists,
// minimum image dimensions and line spacing larger than the cells and margins,
// so callers never receive an unexpectedly sized or null image.
#include "syntheticpage.h"

#include <QGuiApplication>
#include <QImage>

#include <gtest/gtest.h>

using maru::test::SyntheticPage;
using maru::test::SyntheticPageOptions;

namespace
{

// The size syntheticpage.h documents for one line of columns characters: one cell per character
// along the text, one cell across it, plus two margins on each axis.
QSize expectedSize(const SyntheticPageOptions &options, int columns, int lines)
{
    const int alongText =
        (2 * options.margin) + (columns * (options.vertical ? options.cellSize.height() : options.cellSize.width()));
    const int acrossText = (2 * options.margin) + ((lines - 1) * options.lineSpacing) +
                           (options.vertical ? options.cellSize.width() : options.cellSize.height());
    return options.vertical ? QSize{acrossText, alongText} : QSize{alongText, acrossText};
}

} // namespace

TEST(SyntheticPageTest, sizesOneLineToOneCellPerCharacterPlusTwoMargins)
{
    SyntheticPageOptions options;
    options.lines = QStringList{QStringLiteral("日本語")};
    const SyntheticPage page{options};

    EXPECT_EQ(page.image().size(), expectedSize(options, 3, 1));
    EXPECT_EQ(page.boxOf(0, 0),
              QRect(options.margin, options.margin, options.cellSize.width(), options.cellSize.height()));
    EXPECT_EQ(page.boxOf(0, 1).x(), options.margin + options.cellSize.width());
}

// The case syntheticpage.h documents as "a background-only image of one cell plus two margins and
// a truth with no lines", which is 64x64 with the default 32x32 cell and 16 px margin.
TEST(SyntheticPageTest, givesAnEmptyLineListOneCellPlusTwoMargins)
{
    SyntheticPageOptions options;
    options.lines = QStringList{};
    const SyntheticPage page{options};

    EXPECT_FALSE(page.image().isNull());
    EXPECT_EQ(page.image().size(), QSize(64, 64));
    EXPECT_TRUE(page.truth().lines.isEmpty());
    EXPECT_TRUE(page.truth().success);
    EXPECT_EQ(page.truth().sourceSize, page.image().size());
}

// A lineSpacing larger than the margins and the cell together, which underflowed the across-text
// extent to a negative number and produced a null image QPainter refuses to begin on.
TEST(SyntheticPageTest, keepsTheImageValidWhereLineSpacingExceedsTheCellAndMargins)
{
    SyntheticPageOptions options;
    options.lines = QStringList{};
    options.margin = 8;
    options.cellSize = QSize{32, 32};
    options.lineSpacing = 64;
    const SyntheticPage page{options};

    EXPECT_FALSE(page.image().isNull());
    EXPECT_EQ(page.image().size(), QSize(48, 48));
}

// A vertical page swaps the two extents, so the same underflow would have reached the other axis.
TEST(SyntheticPageTest, swapsTheExtentsForAVerticalPage)
{
    SyntheticPageOptions options;
    options.lines = QStringList{QStringLiteral("日本"), QStringLiteral("語")};
    options.vertical = true;
    const SyntheticPage page{options};

    EXPECT_EQ(page.image().size(), expectedSize(options, 2, 2));
    // Lines advance right to left, so line 0 sits at the right edge.
    EXPECT_GT(page.boxOf(0, 0).x(), page.boxOf(1, 0).x());
}

TEST(SyntheticPageTest, reportsOneBoxPerCharacterOfEveryLine)
{
    SyntheticPageOptions options;
    options.lines = QStringList{QStringLiteral("日本語"), QStringLiteral("辞書")};
    const SyntheticPage page{options};

    const maru::ocr::Result truth = page.truth();
    ASSERT_EQ(truth.lines.size(), 2);
    for (const maru::ocr::TextLine &line : truth.lines) {
        // The invariant every backend has to keep, asserted on the double the suites compare
        // against: the hit test indexes the string and the box array with one index.
        EXPECT_EQ(line.text.size(), line.chars.size());
    }
    EXPECT_EQ(truth.lines.at(0).chars.size(), 3);
    EXPECT_EQ(truth.lines.at(1).chars.size(), 2);
}

// pixelSize states a glyph height in image pixels, which is what a case reproducing text of a
// stated size sets. A pixelSize equal to cellSize is the layout full-width Japanese has, where the
// advance of a character equals the em size and the ink of the glyph is smaller than the em; the
// background that difference leaves is what a recognition model segments one character from the
// next by.
//
// Containment inside the cell is not the property asserted here, because
// QPainter::drawText(QRect, flags, text) clips to the rectangle unless Qt::TextDontClip is passed
// and syntheticpage.cpp passes neither. What is asserted is the background that survives.
TEST(SyntheticPageTest, leavesBackgroundAroundAGlyphOfTheStatedPixelHeight)
{
    if (!maru::test::hasJapaneseFont()) {
        GTEST_SKIP() << "fontconfig resolves no family covering U+65E5, so the page carries no Japanese glyphs";
    }
    constexpr int kCellPx = 14;
    SyntheticPageOptions options;
    options.lines = QStringList{QStringLiteral("日")};
    options.cellSize = QSize{kCellPx, kCellPx};
    options.pixelSize = kCellPx;
    options.margin = 8;
    const SyntheticPage page{options};

    // The bounding box of every pixel darker than the white background, which is where the glyph
    // was painted.
    const QImage image = page.image();
    QRect ink;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qGray(image.pixel(x, y)) < 200) {
                ink = ink.united(QRect{x, y, 1, 1});
            }
        }
    }
    ASSERT_FALSE(ink.isEmpty());
    // A glyph smaller than its cell leaves background on both axes. The threshold
    // must use the glyph box rather than counting those background pixels as text.
    EXPECT_LT(ink.width(), kCellPx) << "ink " << ink.width() << " px wide fills the " << kCellPx << " px cell";
    EXPECT_LT(ink.height(), kCellPx) << "ink " << ink.height() << " px tall fills the " << kCellPx << " px cell";
    // The glyph is drawn at the size asked for rather than at a default: half a cell would still
    // leave background, and would be a font size the option failed to apply.
    EXPECT_GE(ink.height(), kCellPx / 2);
}

int main(int argc, char **argv)
{
    QGuiApplication app{argc, argv};
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
