// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "ocr/grouping.h"

#include <gtest/gtest.h>

using namespace maru::ocr;

namespace
{

// One line of size character boxes laid out from origin along the reading axis, which is what
// a recognized line looks like once the character boxes are mapped back.
TextLine line(const QString &text, QPoint origin, int size, bool vertical)
{
    TextLine result;
    result.text = text;
    result.vertical = vertical;
    for (qsizetype index = 0; index < text.size(); ++index) {
        const QRect box = vertical ? QRect{origin.x(), origin.y() + (static_cast<int>(index) * size), size, size}
                                   : QRect{origin.x() + (static_cast<int>(index) * size), origin.y(), size, size};
        result.chars.append(
            CharBox{.codePoint = static_cast<char32_t>(text.at(index).unicode()), .box = box, .confidence = 0.9F});
        result.box = result.box.united(box);
    }
    return result;
}

const QSize kImage{800, 600};

} // namespace

TEST(Grouping, joinsTwoHorizontalLinesIntoOneParagraph)
{
    const QList<TextLine> lines = {
        line(QStringLiteral("これは"), QPoint{0, 0}, 20, false),
        line(QStringLiteral("ペンです"), QPoint{0, 25}, 20, false),
    };
    const QList<Paragraph> paragraphs = groupLines(lines, kImage);
    ASSERT_EQ(paragraphs.size(), 1);
    EXPECT_FALSE(paragraphs.at(0).vertical);
    EXPECT_EQ(paragraphs.at(0).text, QStringLiteral("これはペンです"));
    EXPECT_EQ(paragraphs.at(0).chars.size(), paragraphs.at(0).text.size());
    EXPECT_EQ(paragraphs.at(0).lineStarts, QList<int>({0, 3}));
    EXPECT_EQ(paragraphs.at(0).box, QRect(0, 0, 80, 45));
}

TEST(Grouping, farApartHorizontalLinesStaySeparate)
{
    const QList<TextLine> lines = {
        line(QStringLiteral("これは"), QPoint{0, 0}, 20, false),
        // 200 px below: the centre distance exceeds 1.9 times the taller line's height.
        line(QStringLiteral("ペンです"), QPoint{0, 220}, 20, false),
    };
    EXPECT_EQ(groupLines(lines, kImage).size(), 2);
}

TEST(Grouping, verticalParagraphReadsRightToLeft)
{
    const QList<TextLine> lines = {
        line(QStringLiteral("これはひだり"), QPoint{0, 0}, 20, true),
        line(QStringLiteral("これはみぎがわ"), QPoint{25, 0}, 20, true),
    };
    const QList<Paragraph> paragraphs = groupLines(lines, kImage);
    ASSERT_EQ(paragraphs.size(), 1);
    EXPECT_TRUE(paragraphs.at(0).vertical);
    // The rightmost line comes first, which is the reading order of vertical Japanese.
    EXPECT_EQ(paragraphs.at(0).text, QStringLiteral("これはみぎがわこれはひだり"));
    EXPECT_EQ(paragraphs.at(0).lineStarts, QList<int>({0, 7}));
    EXPECT_EQ(paragraphs.at(0).chars.constFirst().box.x(), 25);
}

TEST(Grouping, narrowVerticalLineBecomesItsOwnFuriganaParagraph)
{
    // Two 20 px wide main lines and one 6 px wide line: the median width is 20 and 6 falls
    // below 0.65 * 20 = 13.
    const QList<TextLine> lines = {
        line(QStringLiteral("これはたてがき"), QPoint{0, 0}, 20, true),
        line(QStringLiteral("これもたてがき"), QPoint{25, 0}, 20, true),
        line(QStringLiteral("ふりがな"), QPoint{50, 0}, 6, true),
    };
    const QList<Paragraph> paragraphs = groupLines(lines, kImage);
    ASSERT_EQ(paragraphs.size(), 2);
    EXPECT_EQ(paragraphs.at(0).text, QStringLiteral("これもたてがきこれはたてがき"));
    // The furigana line is appended last, as a paragraph of its own.
    EXPECT_EQ(paragraphs.at(1).text, QStringLiteral("ふりがな"));
    EXPECT_EQ(paragraphs.at(1).lineStarts, QList<int>({0}));
}

TEST(Grouping, aSingleLineOfAnOrientationIsAlwaysMainText)
{
    // One vertical line of width 6 has no median to fall below and stays main text.
    const QList<TextLine> lines = {line(QStringLiteral("ほそいたて"), QPoint{0, 0}, 6, true)};
    const QList<Paragraph> paragraphs = groupLines(lines, kImage);
    ASSERT_EQ(paragraphs.size(), 1);
    EXPECT_TRUE(paragraphs.at(0).vertical);
}

TEST(Grouping, separatesTheTwoOrientationsAndPutsVerticalFirst)
{
    const QList<TextLine> lines = {
        line(QStringLiteral("よこがきです"), QPoint{300, 300}, 20, false),
        line(QStringLiteral("たてがきです"), QPoint{0, 0}, 20, true),
    };
    const QList<Paragraph> paragraphs = groupLines(lines, kImage);
    ASSERT_EQ(paragraphs.size(), 2);
    EXPECT_TRUE(paragraphs.at(0).vertical);
    EXPECT_EQ(paragraphs.at(0).text, QStringLiteral("たてがきです"));
    EXPECT_FALSE(paragraphs.at(1).vertical);
}

TEST(Grouping, orientationIsRecomputedFromTheCharacterUnion)
{
    // Two characters stacked vertically measure 20 x 40, and 20 * 1.5 = 30 is below 40, so the
    // line groups as vertical even though the recognition pass reported it as horizontal.
    TextLine stacked = line(QStringLiteral("日本"), QPoint{0, 0}, 20, true);
    stacked.vertical = false;
    const QList<Paragraph> paragraphs = groupLines({stacked}, kImage);
    ASSERT_EQ(paragraphs.size(), 1);
    EXPECT_TRUE(paragraphs.at(0).vertical);

    // Three characters side by side measure 60 x 20 and stay horizontal.
    TextLine row = line(QStringLiteral("日本語"), QPoint{0, 0}, 20, false);
    row.vertical = true;
    const QList<Paragraph> horizontal = groupLines({row}, kImage);
    ASSERT_EQ(horizontal.size(), 1);
    EXPECT_FALSE(horizontal.at(0).vertical);
}

TEST(Grouping, dropsLinesWithoutKanaOrKanji)
{
    const QList<TextLine> lines = {
        line(QStringLiteral("Play"), QPoint{0, 0}, 20, false),
        line(QStringLiteral("12345"), QPoint{0, 30}, 20, false),
        line(QStringLiteral("にほんご"), QPoint{0, 60}, 20, false),
    };
    const QList<Paragraph> paragraphs = groupLines(lines, kImage);
    ASSERT_EQ(paragraphs.size(), 1);
    EXPECT_EQ(paragraphs.at(0).text, QStringLiteral("にほんご"));
}

// A Korean caption inside the captured region reaches the recognition pass as its own line.
// jp::isKatakana() spanned U+30A0-U+31FF before this test existed, so a Hangul Compatibility
// Jamo line passed containsJapanese() and reached the lookup.
TEST(Grouping, dropsAHangulLine)
{
    const QList<TextLine> lines = {
        line(QStringLiteral("ㄱㄴㄷ"), QPoint{0, 0}, 20, false),
        line(QStringLiteral("한국어"), QPoint{0, 30}, 20, false),
        line(QStringLiteral("にほんご"), QPoint{0, 60}, 20, false),
    };
    const QList<Paragraph> paragraphs = groupLines(lines, kImage);
    ASSERT_EQ(paragraphs.size(), 1);
    EXPECT_EQ(paragraphs.at(0).text, QStringLiteral("にほんご"));
}

TEST(Grouping, dropsLinesWithoutCharacterBoxes)
{
    TextLine empty;
    empty.text = QStringLiteral("にほんご");
    EXPECT_TRUE(groupLines({empty}, kImage).isEmpty());
}

TEST(Grouping, japaneseRangeCoversKanaAndIdeographs)
{
    EXPECT_TRUE(containsJapanese(QStringLiteral("あ")));
    EXPECT_TRUE(containsJapanese(QStringLiteral("ア")));
    EXPECT_TRUE(containsJapanese(QStringLiteral("語")));
    EXPECT_TRUE(containsJapanese(QStringLiteral("Play にほんご")));
    EXPECT_FALSE(containsJapanese(QStringLiteral("Play 12345")));
    EXPECT_FALSE(containsJapanese(QString{}));
    // Fullwidth Latin and the punctuation jp::isJapanese() accepts carry nothing to look up.
    EXPECT_FALSE(containsJapanese(QStringLiteral("ＰＬＡＹ")));
    EXPECT_FALSE(containsJapanese(QStringLiteral("――…")));
    // The blocks between the Katakana block and Katakana Phonetic Extensions: Bopomofo,
    // Hangul Compatibility Jamo and CJK Strokes.
    EXPECT_FALSE(containsJapanese(QStringLiteral("ㄅㄆㄇ")));
    EXPECT_FALSE(containsJapanese(QStringLiteral("ㄱㄴㄷ")));
    EXPECT_FALSE(containsJapanese(QStringLiteral("한국어")));
    EXPECT_FALSE(containsJapanese(QStringLiteral("㇀㇁")));
    // Kanbun U+3190-U+319F stays accepted through jp::isKanji().
    EXPECT_TRUE(containsJapanese(QStringLiteral("㆐")));
    // Katakana Phonetic Extensions, which the Ainu spellings ㇰ and ㇺ come from.
    EXPECT_TRUE(containsJapanese(QStringLiteral("ㇰ")));
}

TEST(Grouping, medianOfAnEvenCountIsTheMeanOfTheMiddleTwo)
{
    EXPECT_DOUBLE_EQ(median({1.0, 2.0, 3.0, 4.0}), 2.5);
    EXPECT_DOUBLE_EQ(median({3.0, 1.0, 2.0}), 2.0);
    EXPECT_DOUBLE_EQ(median({}), 0.0);
}

TEST(Grouping, adjacencyNeedsBothOverlapAndProximity)
{
    // Horizontal: the x overlap is 100 of 100 and the centre distance is 25 against
    // 1.9 * 20 = 38.
    EXPECT_TRUE(linesAdjacent(QRect{0, 0, 100, 20}, QRect{0, 25, 100, 20}, false));
    // The same proximity with an x overlap of 10 of 100 fails the overlap rule.
    EXPECT_FALSE(linesAdjacent(QRect{0, 0, 100, 20}, QRect{90, 25, 100, 20}, false));
    // Full overlap with a centre distance of 60 fails the proximity rule.
    EXPECT_FALSE(linesAdjacent(QRect{0, 0, 100, 20}, QRect{0, 60, 100, 20}, false));
    // Vertical swaps the two axes.
    EXPECT_TRUE(linesAdjacent(QRect{0, 0, 20, 100}, QRect{25, 0, 20, 100}, true));
    EXPECT_FALSE(linesAdjacent(QRect{0, 0, 20, 100}, QRect{60, 0, 20, 100}, true));
}

TEST(Grouping, boxesAreBoundedByTheImage)
{
    // A character box reaching past the image edge is clipped, so a hit test never reports a
    // paragraph outside the frame that produced it.
    const QList<TextLine> lines = {line(QStringLiteral("にほんご"), QPoint{0, 0}, 20, false)};
    const QList<Paragraph> paragraphs = groupLines(lines, QSize{50, 50});
    ASSERT_EQ(paragraphs.size(), 1);
    EXPECT_EQ(paragraphs.at(0).box, QRect(0, 0, 50, 20));
}
