// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The wl_shm format a captured frame is read back through.
//
// wl::ShmBuffer::reset() needs a wl_shm, and so a compositor, which is why the allocation path is
// covered by tests/platform/hyprlandlive_test.cpp alone. The mapping below needs neither, and it
// is the one part of the buffer that fails silently: an image built with the wrong QImage format
// has the right size, the right stride and a plausible hash, and every colour in it is wrong.
#include "wayland/shmbuffer.h"

#include <QImage>

#include <gtest/gtest.h>
#include <wayland-client-protocol.h>

using namespace maru::wl;

TEST(ShmBufferTest, mapsTheTwoFormatsEveryCompositorOffers)
{
    // wl_shm requires ARGB8888 and XRGB8888 of every compositor, so these two are the pair
    // WlrFrameSource can always accept.
    EXPECT_EQ(ShmBuffer::imageFormatFor(WL_SHM_FORMAT_XRGB8888), QImage::Format_RGB32);
    EXPECT_EQ(ShmBuffer::imageFormatFor(WL_SHM_FORMAT_ARGB8888), QImage::Format_ARGB32_Premultiplied);
}

TEST(ShmBufferTest, mapsAnAlphaFormatToThePremultipliedQtFormat)
{
    // wl_shm's ARGB8888 is premultiplied. QImage::Format_ARGB32 is not, and choosing it would
    // leave every partly transparent pixel of a captured region too bright.
    EXPECT_NE(ShmBuffer::imageFormatFor(WL_SHM_FORMAT_ARGB8888), QImage::Format_ARGB32);
}

TEST(ShmBufferTest, theTwoMandatoryFormatsAreTheOnesWithSmallCodes)
{
    // wl_shm numbers argb8888 0 and xrgb8888 1 and gives every other format its DRM fourcc, so
    // the two mandatory codes are the only ones that are not four packed characters. A test
    // asserting that some small integer means "unsupported" would be asserting the opposite of
    // the protocol.
    static_assert(WL_SHM_FORMAT_ARGB8888 == 0);
    static_assert(WL_SHM_FORMAT_XRGB8888 == 1);
    EXPECT_NE(ShmBuffer::imageFormatFor(0), QImage::Format_Invalid);
    EXPECT_NE(ShmBuffer::imageFormatFor(1), QImage::Format_Invalid);
}

TEST(ShmBufferTest, refusesAFormatThisBuildCannotRead)
{
    // WlrFrameSource skips a buffer event whose format answers Format_Invalid and waits for
    // another, so a format outside the two mandatory ones has to answer exactly that rather than
    // a plausible-looking format that would be read in the wrong byte order. The three below are
    // ones a compositor really offers: a 16-bit packed format, and the two byte-swapped
    // 32-bit ones a wrong mapping would read with red and blue exchanged.
    EXPECT_EQ(ShmBuffer::imageFormatFor(WL_SHM_FORMAT_RGB565), QImage::Format_Invalid);
    EXPECT_EQ(ShmBuffer::imageFormatFor(WL_SHM_FORMAT_XBGR8888), QImage::Format_Invalid);
    EXPECT_EQ(ShmBuffer::imageFormatFor(WL_SHM_FORMAT_ABGR8888), QImage::Format_Invalid);
    EXPECT_EQ(ShmBuffer::imageFormatFor(WL_SHM_FORMAT_XRGB2101010), QImage::Format_Invalid);
}

TEST(ShmBufferTest, theMappedFormatsCarryFourBytesPerPixel)
{
    // WlrFrameSource passes the compositor's stride straight into the QImage, and
    // FrameSource::hashImage() reads width * depth / 8 bytes per row. A 32-bit format is what
    // makes those two agree with the buffer the compositor sized.
    for (const quint32 format : {quint32{WL_SHM_FORMAT_XRGB8888}, quint32{WL_SHM_FORMAT_ARGB8888}}) {
        const QImage probe{1, 1, ShmBuffer::imageFormatFor(format)};
        EXPECT_EQ(probe.depth(), 32) << format;
    }
}

TEST(ShmBufferTest, isEmptyBeforeItIsReset)
{
    const ShmBuffer buffer;
    EXPECT_FALSE(buffer.isValid());
    EXPECT_EQ(buffer.buffer(), nullptr);
    EXPECT_TRUE(buffer.size().isEmpty());
    EXPECT_EQ(buffer.stride(), 0);
    EXPECT_TRUE(buffer.image().isNull());
}

TEST(ShmBufferTest, refusesAnAllocationWithNoShm)
{
    // The guard that keeps a source with no wl_shm from calling wl_shm_create_pool on nullptr.
    ShmBuffer buffer;
    EXPECT_FALSE(buffer.reset(nullptr, QSize{320, 240}, WL_SHM_FORMAT_XRGB8888, 1280));
    EXPECT_FALSE(buffer.isValid());
}
