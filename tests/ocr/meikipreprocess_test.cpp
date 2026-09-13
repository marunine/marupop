// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "ocr/meikipreprocess.h"

#include <QImage>

#include <gtest/gtest.h>
#include <vector>

using namespace maru::ocr;

namespace
{

const QSize detectionTarget{kDetectionWidth, kDetectionHeight};

} // namespace

TEST(MeikiPreprocess, letterboxFitsTheWiderAxis)
{
    // 1920x1080 is wider than 960x544, so the width sets the scale and the height pads.
    const Letterbox landscape = detectionLetterbox(QSize{1920, 1080}, detectionTarget);
    EXPECT_DOUBLE_EQ(landscape.scale, 0.5);
    EXPECT_EQ(landscape.width, 960);
    EXPECT_EQ(landscape.height, 540);
}

TEST(MeikiPreprocess, letterboxUpscalesASmallRegion)
{
    // The scale is not clamped to 1.0: a 480x270 scan region fills the model input, which is
    // what makes small text resolvable.
    const Letterbox upscaled = detectionLetterbox(QSize{480, 270}, detectionTarget);
    EXPECT_DOUBLE_EQ(upscaled.scale, 2.0);
    EXPECT_EQ(upscaled.width, 960);
    EXPECT_EQ(upscaled.height, 540);
}

TEST(MeikiPreprocess, letterboxTruncatesTheScaledExtent)
{
    // 333 * (960 / 700) = 456.68..., truncated to 456 rather than rounded to 457.
    const Letterbox truncated = detectionLetterbox(QSize{700, 333}, detectionTarget);
    EXPECT_EQ(truncated.width, 960);
    EXPECT_EQ(truncated.height, 456);
}

TEST(MeikiPreprocess, letterboxFitsTheSmallDetectorInput)
{
    const Letterbox small = detectionLetterbox(QSize{640, 480}, QSize{kSmallDetectionWidth, kSmallDetectionHeight});
    EXPECT_DOUBLE_EQ(small.scale, 0.4);
    EXPECT_EQ(small.width, 256);
    EXPECT_EQ(small.height, 192);
}

TEST(MeikiPreprocess, letterboxRejectsAnEmptySource)
{
    const Letterbox empty = detectionLetterbox(QSize{0, 0}, detectionTarget);
    EXPECT_EQ(empty.width, 0);
    EXPECT_EQ(empty.height, 0);
}

TEST(MeikiPreprocess, packsRgbPlanesAndZeroesThePadding)
{
    QImage image{2, 1, QImage::Format_RGB888};
    image.setPixelColor(0, 0, QColor{255, 0, 0});
    image.setPixelColor(1, 0, QColor{0, 128, 255});

    const QSize target{4, 2};
    std::vector<float> tensor(static_cast<size_t>(3) * 4 * 2, -1.0F);
    const Letterbox letterbox{.scale = 1.0, .width = 2, .height = 1};
    ASSERT_TRUE(packDetectionTensor(image, target, letterbox, tensor.data()));

    const size_t plane = 8;
    // Red channel of the two content pixels, then green, then blue.
    EXPECT_FLOAT_EQ(tensor[0], 1.0F);
    EXPECT_FLOAT_EQ(tensor[1], 0.0F);
    EXPECT_FLOAT_EQ(tensor[plane + 0], 0.0F);
    EXPECT_FLOAT_EQ(tensor[plane + 1], 128.0F / 255.0F);
    EXPECT_FLOAT_EQ(tensor[(2 * plane) + 0], 0.0F);
    EXPECT_FLOAT_EQ(tensor[(2 * plane) + 1], 1.0F);
    // Everything outside the 2x1 content rectangle is black padding.
    EXPECT_FLOAT_EQ(tensor[2], 0.0F);
    EXPECT_FLOAT_EQ(tensor[plane - 1], 0.0F);
    EXPECT_FLOAT_EQ(tensor[(3 * plane) - 1], 0.0F);
}

TEST(MeikiPreprocess, packConvertsThroughFormatRgb888)
{
    QImage argb{2, 1, QImage::Format_ARGB32};
    argb.fill(QColor{10, 20, 30});
    const QImage converted = toRgb888(argb);
    ASSERT_EQ(converted.format(), QImage::Format_RGB888);

    std::vector<float> tensor(static_cast<size_t>(3) * 2 * 1, -1.0F);
    const Letterbox letterbox{.scale = 1.0, .width = 2, .height = 1};
    ASSERT_TRUE(packDetectionTensor(converted, QSize{2, 1}, letterbox, tensor.data()));
    EXPECT_FLOAT_EQ(tensor[0], 10.0F / 255.0F);
    EXPECT_FLOAT_EQ(tensor[2], 20.0F / 255.0F);
    EXPECT_FLOAT_EQ(tensor[4], 30.0F / 255.0F);
}

TEST(MeikiPreprocess, horizontalCropScalesToThirtyTwoPixelsHigh)
{
    const HorizontalCrop crop = horizontalCrop(QRect{0, 0, 100, 50});
    EXPECT_EQ(crop.effectiveHeight, 32);
    EXPECT_EQ(crop.effectiveWidth, 64); // 100 * (32 / 50)
}

TEST(MeikiPreprocess, horizontalCropCapsTheWidthAndShrinksTheHeight)
{
    // 3000 * (32 / 50) = 1920 exceeds 960, so the width is capped and the height halves.
    const HorizontalCrop crop = horizontalCrop(QRect{0, 0, 3000, 50});
    EXPECT_EQ(crop.effectiveWidth, kRecognitionWidth);
    EXPECT_EQ(crop.effectiveHeight, 16);
}

TEST(MeikiPreprocess, verticalBoxUnderTheLimitIsOneSegment)
{
    // 400 * (32 / 40) = 320 scaled pixels, below the 480 px model input, so no split.
    const QList<VerticalSegment> segments = verticalSegments(QRect{5, 10, 40, 400});
    ASSERT_EQ(segments.size(), 1);
    EXPECT_EQ(segments.at(0).box, QRect(5, 10, 40, 400));
    EXPECT_EQ(segments.at(0).effectiveWidth, kVerticalWidth);
    EXPECT_EQ(segments.at(0).effectiveHeight, 320);
    EXPECT_EQ(segments.at(0).segmentIndex, 0);
}

TEST(MeikiPreprocess, tallVerticalBoxSplitsWithTheTrailingSegment)
{
    // Width 40 gives a scale of 0.8: the segment height is 420 / 0.8 = 525 source pixels and
    // the stride is (420 - 64) / 0.8 = 445 source pixels.
    const QList<VerticalSegment> segments = verticalSegments(QRect{0, 0, 40, 1000});
    ASSERT_EQ(segments.size(), 3);
    EXPECT_EQ(segments.at(0).box.y(), 0);
    EXPECT_EQ(segments.at(1).box.y(), 445);
    // The trailing segment is anchored at the bottom edge: 1000 - 525.
    EXPECT_EQ(segments.at(2).box.y(), 475);
    EXPECT_EQ(segments.at(2).box.y() + segments.at(2).box.height(), 1000);
    for (const VerticalSegment &segment : segments) {
        EXPECT_EQ(segment.effectiveHeight, kVerticalMaxContentHeight);
        EXPECT_EQ(segment.box.height(), 525);
    }
}

TEST(MeikiPreprocess, trailingSegmentIsDroppedWhenItRepeatsTheLastStart)
{
    // Width 32 gives a scale of 1.0: the stride is 356 px and the segment height is 420 px.
    // A height of 777 px puts the bottom-anchored start at 357, which is within 1.0 px of the
    // last computed start of 356, so it is not appended.
    const QList<VerticalSegment> segments = verticalSegments(QRect{0, 0, 32, 777});
    ASSERT_EQ(segments.size(), 2);
    EXPECT_EQ(segments.at(0).box.y(), 0);
    EXPECT_EQ(segments.at(1).box.y(), 356);
    // The trailing 1 px row of the box is covered by no segment, which is the behaviour of the
    // reference implementation.
    EXPECT_EQ(segments.at(1).box.height(), 420);
    EXPECT_EQ(segments.at(1).box.y() + segments.at(1).box.height(), 776);
    EXPECT_EQ(segments.at(1).effectiveHeight, kVerticalMaxContentHeight);
}

TEST(MeikiPreprocess, verticalSegmentsRejectAnEmptyBox)
{
    EXPECT_TRUE(verticalSegments(QRect{0, 0, 0, 100}).isEmpty());
    EXPECT_TRUE(verticalSegments(QRect{0, 0, 32, 0}).isEmpty());
}

TEST(MeikiPreprocess, packsAVerticalSegmentIntoTheFullCanvas)
{
    QImage image{32, 100, QImage::Format_RGB888};
    image.fill(QColor{255, 255, 255});
    const QList<VerticalSegment> segments = verticalSegments(QRect{0, 0, 32, 100});
    ASSERT_EQ(segments.size(), 1);

    std::vector<float> tensor(static_cast<size_t>(3) * kVerticalHeight * kVerticalWidth, -1.0F);
    ASSERT_TRUE(packVerticalTensor(image, segments.at(0), tensor.data()));
    // The content occupies the first 100 rows and the remaining 380 rows are padding.
    EXPECT_FLOAT_EQ(tensor[0], 1.0F);
    const size_t lastContentRow = static_cast<size_t>(segments.at(0).effectiveHeight - 1) * kVerticalWidth;
    EXPECT_FLOAT_EQ(tensor[lastContentRow], 1.0F);
    const size_t firstPadRow = static_cast<size_t>(segments.at(0).effectiveHeight) * kVerticalWidth;
    EXPECT_FLOAT_EQ(tensor[firstPadRow], 0.0F);
}
