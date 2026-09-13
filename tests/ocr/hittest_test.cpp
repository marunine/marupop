// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "ocr/hittest.h"

#include <QElapsedTimer>

#include <algorithm>
#include <cstdio>
#include <gtest/gtest.h>

using namespace maru::ocr;

namespace
{

// A paragraph whose character boxes are 10 px wide with a 10 px gap between them, so the
// extension rule is what decides a point in a gap.
Paragraph horizontalParagraph()
{
    Paragraph paragraph;
    paragraph.text = QStringLiteral("日本語");
    paragraph.vertical = false;
    paragraph.lineStarts = {0};
    const QList<char32_t> codePoints = {U'日', U'本', U'語'};
    for (int index = 0; index < 3; ++index) {
        const QRect box{index * 20, 0, 10, 10};
        paragraph.chars.append(CharBox{.codePoint = codePoints.at(index), .box = box, .confidence = 0.9F});
        paragraph.box = paragraph.box.united(box);
    }
    return paragraph;
}

Paragraph verticalParagraph()
{
    Paragraph paragraph;
    paragraph.text = QStringLiteral("日本");
    paragraph.vertical = true;
    paragraph.lineStarts = {0};
    const QList<char32_t> codePoints = {U'日', U'本'};
    for (int index = 0; index < 2; ++index) {
        const QRect box{0, index * 20, 10, 10};
        paragraph.chars.append(CharBox{.codePoint = codePoints.at(index), .box = box, .confidence = 0.9F});
        paragraph.box = paragraph.box.united(box);
    }
    return paragraph;
}

Result resultOf(const QList<Paragraph> &paragraphs)
{
    Result result;
    result.success = true;
    result.paragraphs = paragraphs;
    result.sourceSize = QSize{200, 200};
    return result;
}

} // namespace

TEST(HitTest, findsTheCharacterUnderThePointer)
{
    const Result result = resultOf({horizontalParagraph()});
    const std::optional<Hit> hit = hitTest(result, QPoint{25, 5});
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->paragraph, 0);
    EXPECT_EQ(hit->charIndex, 1);
}

TEST(HitTest, aPointInTheGapBelongsToTheExtendedBox)
{
    const Result result = resultOf({horizontalParagraph()});
    // x = 15 lies between the first box (0..9) and the second (20..29). The first box is
    // extended rightwards to 19, so the point maps to the first character.
    const std::optional<Hit> hit = hitTest(result, QPoint{15, 5});
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->charIndex, 0);
}

TEST(HitTest, extensionStopsAtTheParagraphEdges)
{
    const Paragraph paragraph = horizontalParagraph();
    // The first character keeps its own left edge and reaches the second character's left edge.
    EXPECT_EQ(extendedCharBox(paragraph, 0), QRect(0, 0, 20, 10));
    // The middle character reaches both neighbours.
    EXPECT_EQ(extendedCharBox(paragraph, 1), QRect(10, 0, 30, 10));
    // The last character reaches back to its predecessor and keeps its own right edge.
    EXPECT_EQ(extendedCharBox(paragraph, 2), QRect(30, 0, 20, 10));
    EXPECT_TRUE(extendedCharBox(paragraph, 3).isNull());
}

TEST(HitTest, verticalExtensionRunsAlongTheYAxis)
{
    const Paragraph paragraph = verticalParagraph();
    EXPECT_EQ(extendedCharBox(paragraph, 0), QRect(0, 0, 10, 20));
    EXPECT_EQ(extendedCharBox(paragraph, 1), QRect(0, 10, 10, 20));

    const Result result = resultOf({paragraph});
    const std::optional<Hit> hit = hitTest(result, QPoint{5, 15});
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->charIndex, 0);
}

TEST(HitTest, extensionDoesNotCrossALineBreak)
{
    Paragraph paragraph;
    paragraph.text = QStringLiteral("日本");
    paragraph.vertical = false;
    paragraph.lineStarts = {0, 1};
    paragraph.chars = {
        CharBox{.codePoint = U'日', .box = QRect{0, 0, 10, 10}, .confidence = 1.0F},
        CharBox{.codePoint = U'本', .box = QRect{0, 40, 10, 10}, .confidence = 1.0F},
    };
    // The two characters are on different lines, so neither box reaches towards the other.
    EXPECT_EQ(extendedCharBox(paragraph, 0), QRect(0, 0, 10, 10));
    EXPECT_EQ(extendedCharBox(paragraph, 1), QRect(0, 40, 10, 10));
}

TEST(HitTest, missesAPointOutsideEveryParagraph)
{
    const Result result = resultOf({horizontalParagraph()});
    EXPECT_FALSE(hitTest(result, QPoint{150, 150}).has_value());
    EXPECT_FALSE(hitTest(Result{}, QPoint{0, 0}).has_value());
}

TEST(HitTest, fallsBackToTheNearestCharacterWithinTheTolerance)
{
    const Result result = resultOf({horizontalParagraph()});
    // 3 px below the paragraph box: outside every box, inside the 6 px tolerance.
    const std::optional<Hit> near = hitTest(result, QPoint{25, 13});
    ASSERT_TRUE(near.has_value());
    EXPECT_EQ(near->charIndex, 1);
    // 20 px below: beyond the tolerance.
    EXPECT_FALSE(hitTest(result, QPoint{25, 30}).has_value());
}

TEST(HitTest, spanRectCoversTheRequestedCharacters)
{
    const Paragraph paragraph = horizontalParagraph();
    EXPECT_EQ(charSpanRect(paragraph, 0, 2), QRect(0, 0, 30, 10));
    EXPECT_EQ(charSpanRect(paragraph, 1, 1), QRect(20, 0, 10, 10));
    // A range reaching past the end is clamped, and an empty range gives a null rectangle.
    EXPECT_EQ(charSpanRect(paragraph, 2, 9), QRect(40, 0, 10, 10));
    EXPECT_TRUE(charSpanRect(paragraph, 0, 0).isNull());
    EXPECT_TRUE(charSpanRect(paragraph, 9, 1).isNull());
}

TEST(HitTest, searchesEveryParagraphThatHoldsThePoint)
{
    Paragraph first = horizontalParagraph();
    Paragraph second = horizontalParagraph();
    for (CharBox &character : second.chars) {
        character.box.translate(0, 100);
    }
    second.box.translate(0, 100);
    const Result result = resultOf({first, second});
    const std::optional<Hit> hit = hitTest(result, QPoint{5, 105});
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->paragraph, 1);
    EXPECT_EQ(hit->charIndex, 0);
}

// Hit testing runs on the GUI thread at the relay's 125 Hz update rate.
// Use a large synthetic page to ensure both hits and misses remain within a
// reasonable fraction of the pointer update budget.
TEST(HitTest, costsUnderAMillisecondOverAFullPage)
{
    constexpr int kCellPx = 20;
    constexpr int kLineSpacingPx = 30;
    constexpr int kLinesPerParagraph = 6;
    constexpr int kRuns = 200;
    const QSize source{1600, 900};

    QList<Paragraph> paragraphs;
    Paragraph paragraph;
    int lineInParagraph = 0;
    for (int row = 0; row * kLineSpacingPx + kCellPx <= source.height(); ++row) {
        paragraph.lineStarts.append(static_cast<int>(paragraph.chars.size()));
        for (int column = 0; (column + 1) * kCellPx <= source.width(); ++column) {
            const QRect box{column * kCellPx, row * kLineSpacingPx, kCellPx, kCellPx};
            paragraph.text.append(QChar{0x3042});
            paragraph.chars.append(CharBox{.codePoint = U'あ', .box = box, .confidence = 1.0F});
            paragraph.box = paragraph.box.united(box);
        }
        if (++lineInParagraph == kLinesPerParagraph) {
            paragraphs.append(paragraph);
            paragraph = Paragraph{};
            lineInParagraph = 0;
        }
    }
    if (!paragraph.chars.isEmpty()) {
        paragraphs.append(paragraph);
    }
    Result result = resultOf(paragraphs);
    result.sourceSize = source;

    int characters = 0;
    for (const Paragraph &one : result.paragraphs) {
        characters += static_cast<int>(one.chars.size());
    }

    // Inside the last character of the last paragraph, which is the longest search a hit takes,
    // and inside the gap below every paragraph, which no paragraph box holds.
    const QRect last = result.paragraphs.constLast().chars.constLast().box;
    const QPoint inside = last.center();
    const QPoint outside{source.width() - 1, result.paragraphs.constLast().box.bottom() + 200};

    QList<double> hits;
    QList<double> misses;
    for (int run = 0; run < kRuns; ++run) {
        QElapsedTimer timer;
        timer.start();
        const std::optional<Hit> hit = hitTest(result, inside);
        hits.append(static_cast<double>(timer.nsecsElapsed()) / 1e6);
        EXPECT_TRUE(hit.has_value());
        timer.restart();
        const std::optional<Hit> miss = hitTest(result, outside);
        misses.append(static_cast<double>(timer.nsecsElapsed()) / 1e6);
        EXPECT_FALSE(miss.has_value());
    }
    std::ranges::sort(hits);
    std::ranges::sort(misses);
    const auto median = [](const QList<double> &values) {
        return values.at(values.size() / 2);
    };
    std::printf("hitTest over %d paragraphs and %d characters in %dx%d: hit %.4f ms p50 %.4f ms max, "
                "miss %.4f ms p50 %.4f ms max\n",
                static_cast<int>(result.paragraphs.size()),
                characters,
                source.width(),
                source.height(),
                median(hits),
                hits.constLast(),
                median(misses),
                misses.constLast());
    std::fflush(stdout);

    EXPECT_LT(median(hits), 1.0);
    EXPECT_LT(median(misses), 1.0);
}
