// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "ocr/meikipostprocess.h"
#include "ocr/meikipreprocess.h"

#include <array>
#include <gtest/gtest.h>

using namespace maru::ocr;

namespace
{

Candidate candidate(char32_t codePoint, int start, int end, float confidence)
{
    return Candidate{.codePoint = codePoint,
                     .box = QRect{start, 0, end - start, 10},
                     .confidence = confidence,
                     .intervalStart = start,
                     .intervalEnd = end};
}

QString textOf(const QList<Candidate> &candidates)
{
    QString text;
    for (const Candidate &entry : candidates) {
        text.append(QChar(static_cast<char16_t>(entry.codePoint)));
    }
    return text;
}

} // namespace

TEST(MeikiDetection, keepsConfidentBoxesClampedAndSortedByTop)
{
    const std::array<float, 12> boxes = {
        10.9F,
        60.2F,
        50.0F,
        80.0F, // second in reading order
        -5.0F,
        10.0F,
        300.0F,
        30.0F, // clamped to the image on both edges
        0.0F,
        0.0F,
        10.0F,
        10.0F, // below the threshold
    };
    const std::array<float, 3> scores = {0.9F, 0.7F, 0.2F};
    const QList<QRect> kept = detectionBoxes(boxes.data(), scores.data(), 3, QSize{200, 100}, 0.5F);

    ASSERT_EQ(kept.size(), 2);
    // Sorted by the top edge: the box at y = 10 comes first.
    EXPECT_EQ(kept.at(0), QRect(0, 10, 200, 20));
    // 10.9 truncates to 10, not 11.
    EXPECT_EQ(kept.at(1), QRect(10, 60, 40, 20));
}

TEST(MeikiMapping, mapsAHorizontalCharacterThroughTheConstantHeight)
{
    const std::array<float, 4> raw = {0.0F, 0.0F, 240.0F, 16.0F};
    const std::optional<Candidate> mapped =
        mapCharacter(U'あ', raw.data(), 0.9F, QRect{100, 50, 200, 40}, 480, 32, false);
    ASSERT_TRUE(mapped.has_value());
    // x maps through effectiveWidth: 240 / 480 * 200 = 100.
    // y maps through the constant 32: 16 / 32 * 40 = 20.
    EXPECT_EQ(mapped->box, QRect(100, 50, 100, 20));
    EXPECT_EQ(mapped->intervalStart, 100);
    EXPECT_EQ(mapped->intervalEnd, 200);
}

TEST(MeikiMapping, horizontalCrossAxisIgnoresTheEffectiveHeight)
{
    const std::array<float, 4> raw = {0.0F, 0.0F, 240.0F, 16.0F};
    const std::optional<Candidate> tall = mapCharacter(U'あ', raw.data(), 0.9F, QRect{0, 0, 200, 40}, 480, 32, false);
    const std::optional<Candidate> squat = mapCharacter(U'あ', raw.data(), 0.9F, QRect{0, 0, 200, 40}, 480, 16, false);
    ASSERT_TRUE(tall.has_value());
    ASSERT_TRUE(squat.has_value());
    EXPECT_EQ(tall->box, squat->box);
}

TEST(MeikiMapping, dropsAHorizontalCharacterInsideThePadding)
{
    const std::array<float, 4> raw = {480.0F, 0.0F, 500.0F, 16.0F};
    EXPECT_FALSE(mapCharacter(U'あ', raw.data(), 0.9F, QRect{0, 0, 200, 40}, 480, 32, false).has_value());
}

TEST(MeikiMapping, clampsAHorizontalCharacterThatCrossesIntoThePadding)
{
    const std::array<float, 4> raw = {240.0F, 0.0F, 600.0F, 32.0F};
    const std::optional<Candidate> mapped = mapCharacter(U'あ', raw.data(), 0.9F, QRect{0, 0, 200, 40}, 480, 32, false);
    ASSERT_TRUE(mapped.has_value());
    // The right edge is clamped to effectiveWidth before the mapping, so it lands on the crop
    // width of 200 rather than beyond it.
    EXPECT_EQ(mapped->intervalEnd, 200);
}

TEST(MeikiMapping, mapsAVerticalCharacterThroughTheConstantWidth)
{
    const std::array<float, 4> raw = {0.0F, 0.0F, 32.0F, 120.0F};
    const std::optional<Candidate> mapped =
        mapCharacter(U'日', raw.data(), 0.8F, QRect{10, 20, 40, 300}, kVerticalWidth, 240, true);
    ASSERT_TRUE(mapped.has_value());
    // x maps through the constant 32: 32 / 32 * 40 = 40.
    // y maps through effectiveHeight: 120 / 240 * 300 = 150.
    EXPECT_EQ(mapped->box, QRect(10, 20, 40, 150));
    EXPECT_EQ(mapped->intervalStart, 20);
    EXPECT_EQ(mapped->intervalEnd, 170);
}

TEST(MeikiMapping, dropsAVerticalCharacterOfZeroHeight)
{
    const std::array<float, 4> raw = {0.0F, 10.0F, 32.0F, 10.0F};
    EXPECT_FALSE(mapCharacter(U'日', raw.data(), 0.8F, QRect{0, 0, 40, 300}, kVerticalWidth, 240, true).has_value());
}

TEST(MeikiNms, suppressesAShortCandidateInsideALongOne)
{
    // The denominator is the shorter interval: 10 / 10 = 1.0 is above the 0.3 threshold, where
    // a union denominator would give 0.1 and keep both.
    QList<Candidate> candidates = {candidate(U'A', 0, 100, 0.9F), candidate(U'B', 0, 10, 0.8F)};
    const QList<Candidate> accepted = intervalNms(candidates, kOverlapThreshold);
    ASSERT_EQ(accepted.size(), 1);
    EXPECT_EQ(accepted.at(0).codePoint, U'A');
}

TEST(MeikiNms, keepsDisjointAndSlightlyOverlappingCandidates)
{
    QList<Candidate> candidates = {
        candidate(U'A', 0, 10, 0.9F),
        candidate(U'B', 10, 20, 0.8F), // touching, treated as disjoint
        candidate(U'C', 18, 30, 0.7F), // 2 / 10 = 0.2, below the threshold
    };
    const QList<Candidate> accepted = intervalNms(candidates, kOverlapThreshold);
    ASSERT_EQ(accepted.size(), 3);
    EXPECT_EQ(textOf(accepted), QStringLiteral("ABC"));
}

TEST(MeikiNms, returnsReadingOrderRegardlessOfConfidence)
{
    QList<Candidate> candidates = {
        candidate(U'C', 40, 50, 0.5F),
        candidate(U'A', 0, 10, 0.9F),
        candidate(U'B', 20, 30, 0.7F),
    };
    const QList<Candidate> accepted = intervalNms(candidates, kOverlapThreshold);
    EXPECT_EQ(textOf(accepted), QStringLiteral("ABC"));
}

TEST(MeikiNms, theHigherConfidenceCandidateWinsAnOverlap)
{
    QList<Candidate> candidates = {candidate(U'X', 0, 10, 0.4F), candidate(U'Y', 1, 11, 0.6F)};
    const QList<Candidate> accepted = intervalNms(candidates, kOverlapThreshold);
    ASSERT_EQ(accepted.size(), 1);
    EXPECT_EQ(accepted.at(0).codePoint, U'Y');
}

TEST(MeikiPunctuation, classifiesJapanesePunctuationAndTheProlongedSoundMark)
{
    EXPECT_TRUE(isPunctuation(U'。')); // Po
    EXPECT_TRUE(isPunctuation(U'、')); // Po
    EXPECT_TRUE(isPunctuation(U'「')); // Ps
    EXPECT_TRUE(isPunctuation(U'」')); // Pe
    // U+30FC is Lm, a letter modifier, and must keep its confidence.
    EXPECT_FALSE(isPunctuation(U'ー'));
    EXPECT_FALSE(isPunctuation(U'あ'));
    EXPECT_FALSE(isPunctuation(U'語'));
}

TEST(MeikiPunctuation, factorScalesPunctuationAlone)
{
    QList<Candidate> candidates = {candidate(U'。', 0, 10, 1.0F), candidate(U'あ', 20, 30, 1.0F)};
    applyPunctuationFactor(candidates, 0.2F);
    EXPECT_FLOAT_EQ(candidates.at(0).confidence, 0.2F);
    EXPECT_FLOAT_EQ(candidates.at(1).confidence, 1.0F);
}

TEST(MeikiPunctuation, factorOfOneChangesNothing)
{
    QList<Candidate> candidates = {candidate(U'。', 0, 10, 1.0F)};
    applyPunctuationFactor(candidates, 1.0F);
    EXPECT_FLOAT_EQ(candidates.at(0).confidence, 1.0F);
}

TEST(MeikiPunctuation, factorLetsAWordCharacterWinAnOverlap)
{
    QList<Candidate> candidates = {candidate(U'。', 0, 10, 0.9F), candidate(U'あ', 0, 10, 0.5F)};
    applyPunctuationFactor(candidates, 0.2F);
    const QList<Candidate> accepted = intervalNms(candidates, kOverlapThreshold);
    ASSERT_EQ(accepted.size(), 1);
    EXPECT_EQ(accepted.at(0).codePoint, U'あ');
}

TEST(MeikiSwappedPairs, swapsTheCodePointsAndKeepsTheBoxes)
{
    QString text = QStringLiteral("お談冗だ");
    QList<CharBox> chars = {
        CharBox{.codePoint = U'お', .box = QRect{0, 0, 10, 10}, .confidence = 1.0F},
        CharBox{.codePoint = U'談', .box = QRect{10, 0, 10, 10}, .confidence = 1.0F},
        CharBox{.codePoint = U'冗', .box = QRect{20, 0, 10, 10}, .confidence = 1.0F},
        CharBox{.codePoint = U'だ', .box = QRect{30, 0, 10, 10}, .confidence = 1.0F},
    };
    fixSwappedPairs(text, chars);
    EXPECT_EQ(text, QStringLiteral("お冗談だ"));
    EXPECT_EQ(chars.at(1).codePoint, U'冗');
    EXPECT_EQ(chars.at(2).codePoint, U'談');
    EXPECT_EQ(chars.at(1).box, QRect(10, 0, 10, 10));
    EXPECT_EQ(chars.at(2).box, QRect(20, 0, 10, 10));
}

// Every occurrence of every pair is corrected. meikiocr's _fix_swapped_pairs calls str.find
// once per pair (meikiocr/ocr.py:481-486) and leaves a second occurrence
// reversed, which is the one behaviour the port diverges on.
TEST(MeikiSwappedPairs, correctsEveryOccurrenceOfEveryPair)
{
    const QString source = QStringLiteral("談冗と談冗、そして攣痙");
    QString text = source;
    QList<CharBox> chars;
    for (qsizetype index = 0; index < source.size(); ++index) {
        chars.append(CharBox{.codePoint = source.at(index).unicode(),
                             .box = QRect{static_cast<int>(index) * 10, 0, 10, 10},
                             .confidence = 1.0F});
    }

    fixSwappedPairs(text, chars);

    EXPECT_EQ(text, QStringLiteral("冗談と冗談、そして痙攣"));
    ASSERT_EQ(text.size(), chars.size());
    for (qsizetype index = 0; index < text.size(); ++index) {
        EXPECT_EQ(chars.at(index).codePoint, text.at(index).unicode()) << index;
        EXPECT_EQ(chars.at(index).box, QRect(static_cast<int>(index) * 10, 0, 10, 10)) << index;
    }
}

TEST(MeikiSwappedPairs, tableHoldsTheEightPairsInInsertionOrder)
{
    ASSERT_EQ(swappedPairs().size(), 8);
    EXPECT_EQ(swappedPairs().constFirst().first, QStringLiteral("儡傀"));
    EXPECT_EQ(swappedPairs().constFirst().second, QStringLiteral("傀儡"));
    EXPECT_EQ(swappedPairs().constLast().first, QStringLiteral("哭慟"));
}

TEST(MeikiSwappedPairs, leavesTextWithoutAPairAlone)
{
    QString text = QStringLiteral("日本語");
    QList<CharBox> chars = {
        CharBox{.codePoint = U'日', .box = {}, .confidence = 1.0F},
        CharBox{.codePoint = U'本', .box = {}, .confidence = 1.0F},
        CharBox{.codePoint = U'語', .box = {}, .confidence = 1.0F},
    };
    fixSwappedPairs(text, chars);
    EXPECT_EQ(text, QStringLiteral("日本語"));
}

TEST(MeikiTextLine, assemblesTextBoxesAndTheUnionBox)
{
    QList<Candidate> candidates = {
        candidate(U'本', 20, 30, 0.8F),
        candidate(U'日', 0, 10, 0.9F),
        candidate(U'。', 0, 10, 0.95F), // punctuation, demoted below 日 and suppressed
    };
    const TextLine line = buildTextLine(candidates, false, 0.2F, kOverlapThreshold);
    EXPECT_EQ(line.text, QStringLiteral("日本"));
    ASSERT_EQ(line.chars.size(), 2);
    EXPECT_EQ(line.chars.at(0).box, QRect(0, 0, 10, 10));
    EXPECT_EQ(line.box, QRect(0, 0, 30, 10));
    EXPECT_FALSE(line.vertical);
    EXPECT_GT(line.confidence, 0.0F);
}
