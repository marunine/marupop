// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The key-filter sidecar over a 100 000 key corpus: every key present, a random absent key
// rejected, and the file readable again after it is closed and reopened.
#include "dict/keyfilter.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>

#include <array>
#include <gtest/gtest.h>
#include <random>

using namespace maru::dict;

namespace
{

// Deterministic hiragana keys of 2 to 12 characters, so a run reproduces its own failures.
QList<QString> buildKeys(int count, quint32 seed)
{
    std::mt19937 generator(seed);
    std::uniform_int_distribution<int> lengths(2, 12);
    std::uniform_int_distribution<int> characters(0x3042, 0x3093);

    QList<QString> keys;
    keys.reserve(count);
    for (int i = 0; i < count; ++i) {
        QString key;
        const int length = lengths(generator);
        for (int j = 0; j < length; ++j)
            key.append(QChar(static_cast<char16_t>(characters(generator))));
        keys.append(key);
    }
    return keys;
}

} // namespace

TEST(DictKeyFilter, HashIsStableAcrossCalls)
{
    EXPECT_EQ(keyFilterHash(QStringLiteral("はしる")), keyFilterHash(QStringLiteral("はしる")));
    EXPECT_NE(keyFilterHash(QStringLiteral("はしる")), keyFilterHash(QStringLiteral("はしろ")));
}

TEST(DictKeyFilter, HoldsEveryKeyAndRejectsAbsentOnes)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("test.keys"));

    const QList<QString> present = buildKeys(100000, 1);
    KeyFilterWriter writer;
    writer.reserve(present.size());
    for (const QString &key : present)
        writer.add(key);
    ASSERT_TRUE(writer.write(path));
    EXPECT_GT(writer.distinctCount(), 0);
    EXPECT_LE(writer.distinctCount(), present.size());
    EXPECT_GE(writer.maxKeyLength(), 2);
    EXPECT_LE(writer.maxKeyLength(), 12);

    KeyFilter filter;
    ASSERT_TRUE(filter.open(path));
    EXPECT_EQ(filter.count(), writer.distinctCount());
    EXPECT_EQ(filter.maxKeyLength(), writer.maxKeyLength());

    for (const QString &key : present)
        EXPECT_TRUE(filter.contains(key)) << key.toStdString();

    // A disjoint corpus of the same shape: at most a handful of 64-bit collisions are possible
    // over 100 000 present keys, and none is expected.
    const QList<QString> absent = buildKeys(100000, 2);
    const QSet<QString> presentSet(present.cbegin(), present.cend());
    int falsePositives = 0;
    for (const QString &key : absent) {
        if (presentSet.contains(key))
            continue;
        if (filter.contains(key))
            ++falsePositives;
    }
    EXPECT_LE(falsePositives, 2);

    // A key longer than the longest stored key is rejected without a probe.
    EXPECT_FALSE(filter.contains(QString(64, QChar(u'あ'))));
}

TEST(DictKeyFilter, ReopensTheSidecar)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("reopen.keys"));

    KeyFilterWriter writer;
    writer.add(QStringLiteral("はしる"));
    writer.add(QStringLiteral("はしる"));
    writer.add(QStringLiteral("たべる"));
    ASSERT_TRUE(writer.write(path));
    EXPECT_EQ(writer.distinctCount(), 2);

    {
        KeyFilter filter;
        ASSERT_TRUE(filter.open(path));
        EXPECT_TRUE(filter.contains(QStringLiteral("はしる")));
        filter.close();
        EXPECT_FALSE(filter.isOpen());
        EXPECT_FALSE(filter.contains(QStringLiteral("はしる")));
    }

    KeyFilter reopened;
    ASSERT_TRUE(reopened.open(path));
    EXPECT_EQ(reopened.count(), 2);
    EXPECT_TRUE(reopened.contains(QStringLiteral("たべる")));
}

TEST(DictKeyFilter, RejectsAnAbsentOrCorruptFile)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    KeyFilter missing;
    EXPECT_FALSE(missing.open(directory.filePath(QStringLiteral("absent.keys"))));

    const QString path = directory.filePath(QStringLiteral("corrupt.keys"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    KeyFilterHeader header;
    header.magic = 0xDEADBEEF;
    header.count = 0;
    file.write(reinterpret_cast<const char *>(&header), sizeof(header));
    file.close();

    KeyFilter corrupt;
    EXPECT_FALSE(corrupt.open(path));
}

// A count whose byte size wraps 64-bit unsigned arithmetic: 2^61 hashes are 2^64 bytes, which is
// 0, so sizeof(KeyFilterHeader) + count * sizeof(quint64) equals the 24 bytes the file has and
// passes a size comparison. The load has to refuse it, or the span covers unmapped memory.
TEST(DictKeyFilter, RejectsACountThatWrapsItsByteSize)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("wrapping.keys"));

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    KeyFilterHeader header;
    header.count = quint64{1} << 61U;
    ASSERT_EQ(sizeof(KeyFilterHeader) + header.count * sizeof(quint64), sizeof(KeyFilterHeader));
    ASSERT_EQ(file.write(reinterpret_cast<const char *>(&header), sizeof(header)), static_cast<qint64>(sizeof(header)));
    file.close();
    ASSERT_EQ(QFileInfo(path).size(), static_cast<qint64>(sizeof(KeyFilterHeader)));

    KeyFilter wrapping;
    EXPECT_FALSE(wrapping.open(path));
    EXPECT_FALSE(wrapping.isOpen());
    EXPECT_EQ(wrapping.count(), 0);
    EXPECT_FALSE(wrapping.contains(QStringLiteral("はしる")));
}

// One hash short of the count the header declares, which is the truncation the old size
// comparison did catch and the new one has to keep catching.
TEST(DictKeyFilter, RejectsATruncatedBody)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("truncated.keys"));

    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    KeyFilterHeader header;
    header.count = 3;
    file.write(reinterpret_cast<const char *>(&header), sizeof(header));
    const std::array<quint64, 2> hashes{1, 2};
    file.write(reinterpret_cast<const char *>(hashes.data()), sizeof(hashes));
    file.close();

    KeyFilter truncated;
    EXPECT_FALSE(truncated.open(path));
}

TEST(DictKeyFilter, EmptyFilterAnswersFalse)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("empty.keys"));

    KeyFilterWriter writer;
    ASSERT_TRUE(writer.write(path));

    KeyFilter filter;
    ASSERT_TRUE(filter.open(path));
    EXPECT_EQ(filter.count(), 0);
    EXPECT_FALSE(filter.contains(QStringLiteral("はしる")));
}
