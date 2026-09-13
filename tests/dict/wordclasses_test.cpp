// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The word-class table: the deconjugation class set, the reading index, the ambiguity rule and the
// CBOR round trip.
#include "dict/wordclasses.h"

#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace maru::dict;

namespace
{

WordClassTable buildTable()
{
    WordClassTable table;
    // The key is the normalized spelling, which is what JmdictImporter writes; indexReadings()
    // adds the reading keys afterwards.
    table.add(QStringLiteral("走る"),
              WordClassEntry{.spelling = QStringLiteral("走る"),
                             .wordClasses = {QStringLiteral("v5r")},
                             .readings = {QStringLiteral("はしる")}});
    table.add(QStringLiteral("食べる"),
              WordClassEntry{.spelling = QStringLiteral("食べる"),
                             .wordClasses = {QStringLiteral("v1")},
                             .readings = {QStringLiteral("たべる")}});
    // Two entries with the same spelling and reading and different classes, which is the
    // 駆ける case JL names.
    table.add(QStringLiteral("駆ける"),
              WordClassEntry{.spelling = QStringLiteral("駆ける"),
                             .wordClasses = {QStringLiteral("v1")},
                             .readings = {QStringLiteral("かける")}});
    table.add(QStringLiteral("駆ける"),
              WordClassEntry{.spelling = QStringLiteral("駆ける"),
                             .wordClasses = {QStringLiteral("v5r")},
                             .readings = {QStringLiteral("かける")}});
    table.indexReadings();
    return table;
}

} // namespace

TEST(DictWordClasses, AcceptsOnlyTheDeconjugationClasses)
{
    EXPECT_TRUE(isDeconjugationWordClass(QStringLiteral("v5r")));
    EXPECT_TRUE(isDeconjugationWordClass(QStringLiteral("adj-ix")));
    EXPECT_TRUE(isDeconjugationWordClass(QStringLiteral("cop")));
    EXPECT_TRUE(isDeconjugationWordClass(QStringLiteral("vz")));
    EXPECT_FALSE(isDeconjugationWordClass(QStringLiteral("n")));
    EXPECT_FALSE(isDeconjugationWordClass(QStringLiteral("adj-na")));
    EXPECT_FALSE(isDeconjugationWordClass(QString()));
}

TEST(DictWordClasses, MatchesTheSpellingAndTheReading)
{
    const WordClassTable table = buildTable();
    EXPECT_TRUE(table.containsTag(QStringLiteral("走る"), QStringLiteral("はしる"), QStringLiteral("v5r")));
    EXPECT_FALSE(table.containsTag(QStringLiteral("走る"), QStringLiteral("はしる"), QStringLiteral("v1")));
    // A different reading does not match.
    EXPECT_FALSE(table.containsTag(QStringLiteral("走る"), QStringLiteral("そうる"), QStringLiteral("v5r")));
    // A different spelling does not match.
    EXPECT_FALSE(table.containsTag(QStringLiteral("奔る"), QStringLiteral("はしる"), QStringLiteral("v5r")));
}

TEST(DictWordClasses, IndexesEveryReading)
{
    const WordClassTable table = buildTable();
    // indexReadings() puts each entry under its readings, so the reading itself is a key.
    EXPECT_FALSE(table.entriesFor(QStringLiteral("たべる")).isEmpty());
    EXPECT_FALSE(table.entriesFor(QStringLiteral("食べる")).isEmpty());
    EXPECT_TRUE(table.containsTag(QStringLiteral("食べる"), QStringLiteral("たべる"), QStringLiteral("v1")));
}

TEST(DictWordClasses, ReturnsNothingForAnAmbiguousHeadword)
{
    const WordClassTable table = buildTable();
    // Two entries match, so the classes cannot be attributed to either.
    EXPECT_TRUE(table.wordClassesFor(QStringLiteral("駆ける"), QStringLiteral("かける")).isEmpty());
    // containsTag() still answers, because a class carried by any matching entry is valid.
    EXPECT_TRUE(table.containsTag(QStringLiteral("駆ける"), QStringLiteral("かける"), QStringLiteral("v1")));
    EXPECT_TRUE(table.containsTag(QStringLiteral("駆ける"), QStringLiteral("かける"), QStringLiteral("v5r")));

    EXPECT_EQ(table.wordClassesFor(QStringLiteral("走る"), QStringLiteral("はしる")),
              QList<QString>{QStringLiteral("v5r")});
    EXPECT_TRUE(table.wordClassesFor(QStringLiteral("存在しない"), QStringLiteral("そんざい")).isEmpty());
}

TEST(DictWordClasses, RoundTripsThroughTheCborFile)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("wordclasses.pos"));

    const WordClassTable original = buildTable();
    ASSERT_TRUE(original.save(path));

    WordClassTable loaded;
    ASSERT_TRUE(loaded.load(path));
    EXPECT_EQ(loaded.keyCount(), original.keyCount());
    EXPECT_TRUE(loaded.containsTag(QStringLiteral("走る"), QStringLiteral("はしる"), QStringLiteral("v5r")));
    EXPECT_TRUE(loaded.containsTag(QStringLiteral("食べる"), QStringLiteral("たべる"), QStringLiteral("v1")));
    EXPECT_EQ(loaded.entriesFor(QStringLiteral("走る")), original.entriesFor(QStringLiteral("走る")));
}

TEST(DictWordClasses, RejectsAMalformedFile)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("broken.pos"));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QByteArrayLiteral("not cbor"));
    file.close();

    WordClassTable table;
    EXPECT_FALSE(table.load(path));
    EXPECT_TRUE(table.isEmpty());
    EXPECT_FALSE(table.load(directory.filePath(QStringLiteral("absent.pos"))));
}

TEST(DictWordClasses, ClearEmptiesTheTable)
{
    WordClassTable table = buildTable();
    EXPECT_FALSE(table.isEmpty());
    table.clear();
    EXPECT_TRUE(table.isEmpty());
    EXPECT_EQ(table.keyCount(), 0);
}
