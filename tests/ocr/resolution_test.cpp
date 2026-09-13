// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The extent a character reaches in detector input across the scan ladder.
// These arithmetic tests use synthetic character boxes and require no recognition model.
#include "ocr/ocrtypes.h"
#include "ocr/resolution.h"

#include <gtest/gtest.h>

using namespace maru::ocr;

namespace
{

const QSize kDetector{960, 544};
const QSize kSmallDetector{320, 192};

// One horizontal paragraph of characters of cellPx by cellPx, laid out from the origin, with the
// detector input every scan of it was fitted into.
Result pageOf(int cellPx, int count, QSize modelInputSize, QSize sourceSize)
{
    Paragraph paragraph;
    for (int index = 0; index < count; ++index) {
        const QRect box{index * cellPx, 0, cellPx, cellPx};
        paragraph.text.append(QChar{static_cast<char16_t>(0x4E00 + index)});
        paragraph.chars.append(
            CharBox{.codePoint = static_cast<char32_t>(0x4E00 + index), .box = box, .confidence = 1.0F});
        paragraph.box = paragraph.box.united(box);
    }

    Result result;
    result.success = true;
    result.paragraphs.append(paragraph);
    result.sourceSize = sourceSize;
    result.modelInputSize = modelInputSize;
    return result;
}

} // namespace

TEST(OcrResolutionTest, scalesTheThresholdWithTheDetectorInput)
{
    EXPECT_NEAR(minimumCharExtent(kDetector), 23.936, 0.001);
    EXPECT_NEAR(minimumCharExtent(kSmallDetector), 8.448, 0.001);
    // An empty size reports a backend detecting at the resolution of the image it was given,
    // which ScreenAiBackend does by tiling.
    EXPECT_EQ(minimumCharExtent(QSize{}), 0.0);
}

TEST(OcrResolutionTest, takesTheCrossAxisExtentOfTheCharacterBoxes)
{
    const Result horizontal = pageOf(14, 8, kDetector, QSize{480, 270});
    EXPECT_EQ(medianCharExtent(horizontal), 14.0);

    // A vertical paragraph is measured across the reading axis, which is its width.
    Result vertical = horizontal;
    vertical.paragraphs[0].vertical = true;
    for (int index = 0; index < vertical.paragraphs[0].chars.size(); ++index) {
        vertical.paragraphs[0].chars[index].box = QRect{0, index * 14, 20, 14};
    }
    EXPECT_EQ(medianCharExtent(vertical), 20.0);

    EXPECT_EQ(medianCharExtent(Result{}), 0.0);
}

// The three rungs of the default ladder over the 14 px characters the reported case carried:
// 480x270, 960x540 and 1600x900, at InitialScanWidth 480 and MaxScanWidth 1600.
TEST(OcrResolutionTest, reportsTheExtentOfEachRungOfTheLadder)
{
    const Result page = pageOf(14, 30, kDetector, QSize{480, 270});

    EXPECT_NEAR(charExtentAtModelInput(page, QSize{480, 270}), 28.0, 0.01);
    EXPECT_NEAR(charExtentAtModelInput(page, QSize{960, 540}), 14.0, 0.01);
    EXPECT_NEAR(charExtentAtModelInput(page, QSize{1600, 900}), 8.4, 0.01);

    EXPECT_TRUE(resolvesCharactersAt(page, QSize{480, 270}));
    EXPECT_FALSE(resolvesCharactersAt(page, QSize{960, 540}));
    EXPECT_FALSE(resolvesCharactersAt(page, QSize{1600, 900}));
}

// For 28 px characters, the ladder can climb one rung before the detector extent
// falls below the required threshold. This is the character size used by hoverprobe.
TEST(OcrResolutionTest, admitsOneRungForTwentyEightPixelCharacters)
{
    const Result page = pageOf(28, 16, kDetector, QSize{480, 270});

    EXPECT_TRUE(resolvesCharactersAt(page, QSize{480, 270}));
    EXPECT_TRUE(resolvesCharactersAt(page, QSize{960, 540}));
    EXPECT_FALSE(resolvesCharactersAt(page, QSize{1600, 900}));
}

// The small detector takes a third of the input extent and a third of the threshold with it, so
// the same page admits the same rungs under MeikiUseSmallDetector.
TEST(OcrResolutionTest, admitsTheSameRungsOnTheSmallDetector)
{
    const Result page = pageOf(14, 30, kSmallDetector, QSize{480, 270});

    EXPECT_NEAR(charExtentAtModelInput(page, QSize{480, 270}), 9.33, 0.01);
    EXPECT_TRUE(resolvesCharactersAt(page, QSize{480, 270}));
    EXPECT_FALSE(resolvesCharactersAt(page, QSize{960, 540}));
}

// Two unmeasured pairs, each of which reports that the region resolves: a backend that names no
// detector input, and a pass that recognized no paragraph.
TEST(OcrResolutionTest, reportsAnUnmeasuredPairAsResolved)
{
    const Result tiled = pageOf(14, 30, QSize{}, QSize{480, 270});
    EXPECT_EQ(charExtentAtModelInput(tiled, QSize{1600, 900}), 0.0);
    EXPECT_TRUE(resolvesCharactersAt(tiled, QSize{1600, 900}));

    Result blank;
    blank.success = true;
    blank.sourceSize = QSize{480, 270};
    blank.modelInputSize = kDetector;
    EXPECT_EQ(charExtentAtModelInput(blank, QSize{1600, 900}), 0.0);
    EXPECT_TRUE(resolvesCharactersAt(blank, QSize{1600, 900}));
}
