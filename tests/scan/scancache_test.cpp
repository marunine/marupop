// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The recognition-result cache: the key is the pair of region and frame hash, and the eviction
// order is least-recently-used with a hit counting as a use.
#include "scan/scancache.h"

#include <QRect>

#include <gtest/gtest.h>

using namespace maru::scan;

namespace
{

maru::ocr::Result resultWith(const QString &text)
{
    maru::ocr::Paragraph paragraph;
    paragraph.text = text;
    maru::ocr::Result result;
    result.success = true;
    result.paragraphs.append(paragraph);
    return result;
}

QString textOf(const CachedScan *entry)
{
    if (entry == nullptr || entry->result.paragraphs.isEmpty()) {
        return {};
    }
    return entry->result.paragraphs.first().text;
}

const QRect kRect{100, 100, 480, 270};

} // namespace

TEST(ScanCacheTest, findsAnEntryByRegionAndHash)
{
    ScanCache cache;
    cache.insert(kRect, 42, 2.0, resultWith(QStringLiteral("日本語")));

    const CachedScan *entry = cache.find(kRect, 42);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(textOf(entry), QStringLiteral("日本語"));
    EXPECT_EQ(entry->logicalRect, kRect);
    EXPECT_EQ(entry->hash, 42U);
    EXPECT_DOUBLE_EQ(entry->scale, 2.0);

    // The frame the coordinate mappings read carries the geometry and no image.
    const maru::capture::Frame frame = entry->frame();
    EXPECT_EQ(frame.logicalRect, kRect);
    EXPECT_DOUBLE_EQ(frame.scale, 2.0);
    EXPECT_TRUE(frame.image.isNull());
}

TEST(ScanCacheTest, missesOnADifferentHashOrADifferentRegion)
{
    ScanCache cache;
    cache.insert(kRect, 42, 1.0, resultWith(QStringLiteral("日本語")));

    // Same region, different pixels: the screen changed under the pointer.
    EXPECT_EQ(cache.find(kRect, 43), nullptr);
    // Same pixels, different region: a solid-colour rect hashes the same at two sizes.
    EXPECT_EQ(cache.find(kRect.translated(10, 0), 42), nullptr);
}

TEST(ScanCacheTest, replacesAnEntryWithTheSameKey)
{
    ScanCache cache;
    cache.insert(kRect, 42, 1.0, resultWith(QStringLiteral("first")));
    cache.insert(kRect, 42, 1.0, resultWith(QStringLiteral("second")));

    EXPECT_EQ(cache.size(), 1);
    EXPECT_EQ(textOf(cache.find(kRect, 42)), QStringLiteral("second"));
}

TEST(ScanCacheTest, dropsTheLeastRecentlyUsedEntryPastTheCapacity)
{
    ScanCache cache{3};
    EXPECT_EQ(cache.capacity(), 3);
    for (quint64 index = 0; index < 3; ++index) {
        cache.insert(kRect.translated(static_cast<int>(index), 0), index, 1.0, resultWith(QString::number(index)));
    }
    ASSERT_EQ(cache.size(), 3);

    // A hit on the oldest entry makes it the newest, so the next insert evicts the one after it.
    ASSERT_NE(cache.find(kRect, 0), nullptr);
    cache.insert(kRect.translated(3, 0), 3, 1.0, resultWith(QStringLiteral("3")));

    EXPECT_EQ(cache.size(), 3);
    EXPECT_NE(cache.find(kRect, 0), nullptr);
    EXPECT_EQ(cache.find(kRect.translated(1, 0), 1), nullptr);
    EXPECT_NE(cache.find(kRect.translated(2, 0), 2), nullptr);
    EXPECT_NE(cache.find(kRect.translated(3, 0), 3), nullptr);
}

TEST(ScanCacheTest, invalidateDropsEveryOverlappingRegion)
{
    ScanCache cache;
    cache.insert(QRect{0, 0, 100, 100}, 1, 1.0, resultWith(QStringLiteral("a")));
    cache.insert(QRect{50, 50, 100, 100}, 2, 1.0, resultWith(QStringLiteral("b")));
    cache.insert(QRect{500, 500, 100, 100}, 3, 1.0, resultWith(QStringLiteral("c")));

    cache.invalidate(QRect{120, 120, 20, 20});
    EXPECT_EQ(cache.size(), 2);
    EXPECT_NE(cache.find(QRect{0, 0, 100, 100}, 1), nullptr);
    EXPECT_EQ(cache.find(QRect{50, 50, 100, 100}, 2), nullptr);
    EXPECT_NE(cache.find(QRect{500, 500, 100, 100}, 3), nullptr);

    cache.invalidate({});
    EXPECT_EQ(cache.size(), 2);
}

TEST(ScanCacheTest, clearEmptiesTheCache)
{
    ScanCache cache;
    cache.insert(kRect, 42, 1.0, resultWith(QStringLiteral("日本語")));
    cache.clear();
    EXPECT_EQ(cache.size(), 0);
    EXPECT_EQ(cache.find(kRect, 42), nullptr);
}
