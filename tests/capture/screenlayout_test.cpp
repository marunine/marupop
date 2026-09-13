// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "capture/screenlayout.h"

#include <QPainter>

#include <gtest/gtest.h>

using namespace maru::capture;

namespace
{

QImage solidImage(const QSize &physicalSize, qreal dpr, const QColor &color)
{
    QImage image{physicalSize, QImage::Format_RGBA8888_Premultiplied};
    image.fill(color);
    image.setDevicePixelRatio(dpr);
    return image;
}

// A 2560×1440 1x primary with a 1920×1080 2x screen to its right (logical 960×540).
QList<ScreenSlice> mixedLayout()
{
    return {
        {.image = solidImage({2560, 1440}, 1.0, Qt::red),
         .logicalRect = QRectF{0, 0, 2560, 1440},
         .screenName = QStringLiteral("DP-1")},
        {.image = solidImage({1920, 1080}, 2.0, Qt::blue),
         .logicalRect = QRectF{2560, 0, 960, 540},
         .screenName = QStringLiteral("DP-2")},
    };
}

} // namespace

TEST(ScreenLayout, dprRounding)
{
    // At DPR 1.25 the hardware grid is 0.8 logical units.
    EXPECT_DOUBLE_EQ(ScreenLayout::dpx(1.25), 0.8);
    EXPECT_DOUBLE_EQ(ScreenLayout::dprRound(1.0, 1.25), 0.8);
    EXPECT_DOUBLE_EQ(ScreenLayout::dprCeil(0.9, 1.25), 1.6);
    EXPECT_DOUBLE_EQ(ScreenLayout::dprFloor(0.9, 1.25), 0.8);
    const QRectF rounded = ScreenLayout::dprRound(QRectF{0.5, 0.5, 1.0, 1.0}, 1.0);
    EXPECT_DOUBLE_EQ(rounded.left(), 1.0);
    EXPECT_DOUBLE_EQ(rounded.right(), 2.0);
}

TEST(ScreenLayout, boundingAndLookup)
{
    const auto slices = mixedLayout();
    EXPECT_EQ(ScreenLayout::boundingRect(slices), QRectF(0, 0, 3520, 1440));
    ASSERT_NE(ScreenLayout::sliceAt(slices, {100, 100}), nullptr);
    EXPECT_EQ(ScreenLayout::sliceAt(slices, {100, 100})->screenName, QStringLiteral("DP-1"));
    EXPECT_EQ(ScreenLayout::sliceAt(slices, {2600, 100})->screenName, QStringLiteral("DP-2"));
    EXPECT_EQ(ScreenLayout::sliceAt(slices, {2600, 1000}), nullptr); // below the 2x screen
    EXPECT_EQ(ScreenLayout::sliceContaining(slices, QRectF(10, 10, 100, 100))->screenName, QStringLiteral("DP-1"));
    EXPECT_EQ(ScreenLayout::sliceContaining(slices, QRectF(2500, 10, 200, 100)), nullptr); // spans both
}

TEST(ScreenLayout, combinedImageSameDpr)
{
    const QList<ScreenSlice> slices{
        {.image = solidImage({100, 100}, 1.0, Qt::red),
         .logicalRect = QRectF{0, 0, 100, 100},
         .screenName = QStringLiteral("A")},
        {.image = solidImage({100, 100}, 1.0, Qt::blue),
         .logicalRect = QRectF{100, 0, 100, 100},
         .screenName = QStringLiteral("B")},
    };
    const QImage combined = ScreenLayout::combinedImage(slices);
    EXPECT_EQ(combined.size(), QSize(200, 100));
    EXPECT_DOUBLE_EQ(combined.devicePixelRatio(), 1.0);
    EXPECT_EQ(combined.pixelColor(50, 50), QColor(Qt::red));
    EXPECT_EQ(combined.pixelColor(150, 50), QColor(Qt::blue));
}

TEST(ScreenLayout, combinedImageMixedDprScalesUp)
{
    const QImage combined = ScreenLayout::combinedImage(mixedLayout());
    // Target DPR ceil(2.0) = 2: 3520×1440 logical becomes 7040×2880 physical.
    EXPECT_DOUBLE_EQ(combined.devicePixelRatio(), 2.0);
    EXPECT_EQ(combined.size(), QSize(7040, 2880));
    EXPECT_EQ(combined.pixelColor(100, 100), QColor(Qt::red));
    EXPECT_EQ(combined.pixelColor(5200, 100), QColor(Qt::blue));
    // The dead zone below the 2x screen stays transparent.
    EXPECT_EQ(combined.pixelColor(5200, 2000).alpha(), 0);
}

TEST(ScreenLayout, cropWithinOneSliceKeepsNativeDpr)
{
    const QImage cropped = ScreenLayout::cropLogical(mixedLayout(), QRectF{2600, 100, 50, 40});
    EXPECT_DOUBLE_EQ(cropped.devicePixelRatio(), 2.0);
    EXPECT_EQ(cropped.size(), QSize(100, 80)); // physical
    EXPECT_EQ(cropped.pixelColor(0, 0), QColor(Qt::blue));
}

TEST(ScreenLayout, cropSpanningSlicesUsesCombined)
{
    const QImage cropped = ScreenLayout::cropLogical(mixedLayout(), QRectF{2550, 0, 20, 20});
    EXPECT_DOUBLE_EQ(cropped.devicePixelRatio(), 2.0);
    EXPECT_EQ(cropped.size(), QSize(40, 40));
    EXPECT_EQ(cropped.pixelColor(0, 0), QColor(Qt::red));
    EXPECT_EQ(cropped.pixelColor(39, 0), QColor(Qt::blue));
}

TEST(ScreenLayout, colorAtSamplesThePhysicalPixel)
{
    QImage image = solidImage({100, 100}, 2.0, Qt::black);
    image.setPixelColor(99, 99, Qt::green);
    const QList<ScreenSlice> slices{
        {.image = image, .logicalRect = QRectF{0, 0, 50, 50}, .screenName = QStringLiteral("A")}};
    EXPECT_EQ(ScreenLayout::colorAt(slices, {49.9, 49.9}), QColor(Qt::green));
    EXPECT_EQ(ScreenLayout::colorAt(slices, {10, 10}), QColor(Qt::black));
    EXPECT_FALSE(ScreenLayout::colorAt(slices, {60, 60}).isValid());
}
