// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The JMdict importer over JL's 16-entry MockJMdict.xml (tests/data/dict/MockJMdict.xml, copied
// from JL.Core.Tests/Resources): the entry explosion, the re_restr and stagr filtering, the shared
// spelling group, the entity short names and the priority rank.
#include "dict/importers/jmdictimporter.h"
#include "dict/keynorm.h"
#include "dict/records.h"
#include "dict/store.h"
#include "dict/wordclasses.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <atomic>
#include <gtest/gtest.h>

using namespace maru::dict;

namespace
{

QString fixture(const QString &name)
{
    return QStringLiteral(MARUPOP_DICT_TEST_DATA_DIR) + QLatin1Char('/') + name;
}

class JmdictImportFixture : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("jmdict.db"));

        StoreWriter writer;
        ASSERT_TRUE(writer.begin(path, DictType::JMdict));
        std::atomic_bool cancel{false};
        result = importer.import(fixture(QStringLiteral("MockJMdict.xml")), writer, {}, cancel);
        ASSERT_TRUE(result.ok) << result.errorString.toStdString();
        ASSERT_EQ(store.open(path), OpenResult::Ok);
    }

    // The JMdict records stored under key, ignoring the kanji-example records.
    [[nodiscard]] QList<const JmdictRecord *> recordsFor(const QString &key)
    {
        held = store.find(normalizeKey(key));
        QList<const JmdictRecord *> records;
        for (const auto &record : held) {
            if (const JmdictRecord *jmdict = asJmdict(*record))
                records.append(jmdict);
        }
        return records;
    }

    QTemporaryDir directory;
    JmdictImporter importer{true};
    ImportResult result;
    Store store;
    std::vector<std::shared_ptr<const Record>> held;
};

} // namespace

TEST(DictJmdictPriority, RankFollowsTheDocumentedFormula)
{
    EXPECT_EQ(jmdictPriorityRank({}), 0);
    EXPECT_EQ(jmdictPriorityRank({QStringLiteral("nf01")}), 500);
    EXPECT_EQ(jmdictPriorityRank({QStringLiteral("nf48")}), 24000);
    EXPECT_EQ(jmdictPriorityRank({QStringLiteral("ichi1")}), 12000);
    EXPECT_EQ(jmdictPriorityRank({QStringLiteral("news2")}), 24000);
    EXPECT_EQ(jmdictPriorityRank({QStringLiteral("spec1")}), 12000);
    EXPECT_EQ(jmdictPriorityRank({QStringLiteral("gai2")}), 24000);
    // The smallest rank among the tags wins: nf03 is the narrower statement about the same corpus
    // as news1.
    EXPECT_EQ(jmdictPriorityRank({QStringLiteral("ichi1"), QStringLiteral("news1"), QStringLiteral("nf03")}), 1500);
    EXPECT_EQ(jmdictPriorityRank({QStringLiteral("unknown-tag")}), 0);
}

TEST_F(JmdictImportFixture, ImportsEveryEntry)
{
    EXPECT_GT(result.recordCount, 16);
    EXPECT_GT(result.keyCount, 16);
    EXPECT_GT(result.maxKeyLength, 1);
    EXPECT_EQ(store.recordCount(), result.recordCount);
}

TEST_F(JmdictImportFixture, ExplodesTheThreeReadingsOfTheKanjiHi)
{
    // 日 appears in three entries of the mock: 1463770 read ひ, 2083100 read にち and 2083110 read
    // か. Each contributes one record under the kanji key.
    const QList<const JmdictRecord *> kanjiRecords = recordsFor(QStringLiteral("日"));
    ASSERT_EQ(kanjiRecords.size(), 3);
    QList<qint32> entryIds;
    for (const JmdictRecord *record : kanjiRecords) {
        EXPECT_EQ(record->primarySpelling, QStringLiteral("日"));
        entryIds.append(record->entryId);
    }
    EXPECT_TRUE(entryIds.contains(1463770));
    EXPECT_TRUE(entryIds.contains(2083100));
    EXPECT_TRUE(entryIds.contains(2083110));

    // Each reading is a key of its own entry's record and of no other.
    const QList<const JmdictRecord *> hi = recordsFor(QStringLiteral("ひ"));
    ASSERT_FALSE(hi.isEmpty());
    EXPECT_EQ(hi.first()->entryId, 1463770);
    EXPECT_EQ(hi.first()->readings, QList<QString>{QStringLiteral("ひ")});

    const QList<const JmdictRecord *> nichi = recordsFor(QStringLiteral("にち"));
    ASSERT_FALSE(nichi.isEmpty());
    EXPECT_EQ(nichi.first()->entryId, 2083100);

    const QList<const JmdictRecord *> ka = recordsFor(QStringLiteral("か"));
    bool foundKa = false;
    for (const JmdictRecord *record : ka) {
        if (record->entryId == 2083110)
            foundKa = true;
    }
    EXPECT_TRUE(foundKa);
}

TEST_F(JmdictImportFixture, KeepsThePriorityRankOfEachHeadword)
{
    // 日 in entry 1463770 carries ichi1, news1 and nf03, so its rank is 3 * 500.
    const QList<const JmdictRecord *> kanjiRecords = recordsFor(QStringLiteral("日"));
    ASSERT_FALSE(kanjiRecords.isEmpty());
    for (const JmdictRecord *record : kanjiRecords) {
        if (record->entryId == 1463770)
            EXPECT_EQ(record->priorityRank, 1500);
        if (record->entryId == 2083100)
            EXPECT_EQ(record->priorityRank, 12000); // spec1
        if (record->entryId == 2083110)
            EXPECT_EQ(record->priorityRank, 0); // no priority tag
    }
}

TEST_F(JmdictImportFixture, SharesTheSpellingGroupOfHaikyo)
{
    // Entry 1472050 has two kanji elements, 廃墟 and 廃虚, and one reading. Each becomes a record
    // naming the other as an alternative spelling.
    const QList<const JmdictRecord *> primary = recordsFor(QStringLiteral("廃墟"));
    ASSERT_EQ(primary.size(), 1);
    EXPECT_EQ(primary.first()->primarySpelling, QStringLiteral("廃墟"));
    EXPECT_EQ(primary.first()->alternativeSpellings, QList<QString>{QStringLiteral("廃虚")});
    EXPECT_EQ(primary.first()->readings, QList<QString>{QStringLiteral("はいきょ")});
    // 廃墟 carries news2 and nf38, so 38 * 500.
    EXPECT_EQ(primary.first()->priorityRank, 19000);

    const QList<const JmdictRecord *> alternative = recordsFor(QStringLiteral("廃虚"));
    ASSERT_EQ(alternative.size(), 1);
    EXPECT_EQ(alternative.first()->primarySpelling, QStringLiteral("廃虚"));
    EXPECT_EQ(alternative.first()->alternativeSpellings, QList<QString>{QStringLiteral("廃墟")});
    // 廃虚 carries news1 and nf22, so 22 * 500.
    EXPECT_EQ(alternative.first()->priorityRank, 11000);

    // The reading key reaches a record whose primary spelling is the first kanji element.
    const QList<const JmdictRecord *> byReading = recordsFor(QStringLiteral("はいきょ"));
    ASSERT_EQ(byReading.size(), 1);
    EXPECT_EQ(byReading.first()->primarySpelling, QStringLiteral("廃墟"));
}

TEST_F(JmdictImportFixture, FiltersSensesByReadingRestriction)
{
    // Entry 1584930 (余り) restricts its first sense to the reading あまり with a stagr, so the
    // record reached by あんまり drops it and the record reached by あまり keeps it.
    const QList<const JmdictRecord *> amari = recordsFor(QStringLiteral("あまり"));
    ASSERT_FALSE(amari.isEmpty());
    const QList<const JmdictRecord *> anmari = recordsFor(QStringLiteral("あんまり"));
    ASSERT_FALSE(anmari.isEmpty());

    const JmdictRecord *amariRecord = nullptr;
    for (const JmdictRecord *record : amari) {
        if (record->entryId == 1584930)
            amariRecord = record;
    }
    const JmdictRecord *anmariRecord = nullptr;
    for (const JmdictRecord *record : anmari) {
        if (record->entryId == 1584930)
            anmariRecord = record;
    }
    ASSERT_NE(amariRecord, nullptr);
    ASSERT_NE(anmariRecord, nullptr);
    EXPECT_GT(amariRecord->definitions.size(), anmariRecord->definitions.size());
    ASSERT_FALSE(amariRecord->definitions.isEmpty());
    EXPECT_EQ(amariRecord->definitions.first().first(), QStringLiteral("remainder"));
}

TEST_F(JmdictImportFixture, KeepsShortEntityNamesAndTheirDescriptions)
{
    const QList<const JmdictRecord *> records = recordsFor(QStringLiteral("日"));
    ASSERT_FALSE(records.isEmpty());
    bool sawNoun = false;
    for (const JmdictRecord *record : records) {
        for (const QString &wordClass : record->wordClasses.sharedByAllSenses) {
            if (wordClass == QLatin1String("n"))
                sawNoun = true;
            EXPECT_LT(wordClass.size(), 12) << wordClass.toStdString();
        }
    }
    EXPECT_TRUE(sawNoun);

    // The description is stored once, in the meta table.
    const QMap<QString, QString> &entities = importer.entities();
    ASSERT_TRUE(entities.contains(QStringLiteral("n")));
    EXPECT_TRUE(entities.value(QStringLiteral("n")).contains(QStringLiteral("noun")));

    const QJsonObject stored = QJsonDocument::fromJson(store.meta(metakeys::entities).toUtf8()).object();
    EXPECT_EQ(stored.value(QStringLiteral("n")).toString(), entities.value(QStringLiteral("n")));
}

TEST_F(JmdictImportFixture, FactorsTheSharedPartOfSpeechOfAMultiSenseEntry)
{
    // Every sense of 日 in entry 1463770 is a noun, so the array is stored once.
    const QList<const JmdictRecord *> records = recordsFor(QStringLiteral("ひ"));
    const JmdictRecord *record = nullptr;
    for (const JmdictRecord *candidate : records) {
        if (candidate->entryId == 1463770)
            record = candidate;
    }
    ASSERT_NE(record, nullptr);
    EXPECT_GT(record->definitions.size(), 1);
    EXPECT_EQ(record->wordClasses.sharedByAllSenses, QList<QString>{QStringLiteral("n")});
    EXPECT_TRUE(record->wordClasses.perSense.isEmpty());
    // The s_inf of the last sense survives as a per-sense entry.
    ASSERT_EQ(record->definitionInfo.size(), record->definitions.size());
    EXPECT_FALSE(record->definitionInfo.last().isEmpty());
}

TEST_F(JmdictImportFixture, PrefixesCrossReferencesByType)
{
    const QList<const JmdictRecord *> records = recordsFor(QStringLiteral("にち"));
    ASSERT_FALSE(records.isEmpty());
    bool sawCrossReference = false;
    for (const JmdictRecord *record : records) {
        for (const QList<QString> &sense : record->crossReferences) {
            for (const QString &reference : sense) {
                sawCrossReference = true;
                EXPECT_FALSE(reference.startsWith(QLatin1Char(':')));
            }
        }
    }
    EXPECT_TRUE(sawCrossReference);
}

TEST_F(JmdictImportFixture, StoresKanjiExamplesUnderTheExtrasKey)
{
    // 廃 appears in 廃墟 and 廃虚, both of which carry a priority tag.
    const auto records = store.find(kanjiExamplesKey(QStringLiteral("廃")));
    ASSERT_EQ(records.size(), 1U);
    const KanjiExamplesRecord *examples = asKanjiExamples(*records.front());
    ASSERT_NE(examples, nullptr);
    EXPECT_EQ(examples->kanji, QStringLiteral("廃"));
    // The most common headword comes first: 廃虚 ranks 11000 and 廃墟 19000.
    EXPECT_EQ(examples->examples.first().spelling, QStringLiteral("廃虚"));
    EXPECT_EQ(examples->examples.first().reading, QStringLiteral("はいきょ"));
    EXPECT_GE(examples->examples.size(), 1);
    EXPECT_LE(examples->examples.size(), 3);
    for (const KanjiExample &example : examples->examples) {
        EXPECT_GE(example.spelling.size(), 2);
        EXPECT_FALSE(example.gloss.isEmpty());
    }

    // The extras key never collides with a normalized search key.
    EXPECT_NE(kanjiExamplesKey(QStringLiteral("廃")), QStringLiteral("廃"));
    EXPECT_TRUE(store.find(kanjiExamplesKey(QStringLiteral("鬱"))).empty());
    // 日 appears in the mock only as a one-character headword, which is the kanji itself rather
    // than an example word, so it carries no examples.
    EXPECT_TRUE(store.find(kanjiExamplesKey(QStringLiteral("日"))).empty());
}

TEST_F(JmdictImportFixture, BuildsTheWordClassTable)
{
    const WordClassTable &table = importer.wordClassTable();
    EXPECT_GT(table.keyCount(), 0);

    // 始まる is a godan verb in the mock, so the deconjugation gate accepts v5r for it.
    EXPECT_TRUE(table.containsTag(QStringLiteral("始まる"), QStringLiteral("はじまる"), QStringLiteral("v5r")));
    EXPECT_FALSE(table.containsTag(QStringLiteral("始まる"), QStringLiteral("はじまる"), QStringLiteral("v1")));
    // 懐かしい is an i-adjective.
    EXPECT_TRUE(table.containsTag(QStringLiteral("懐かしい"), QStringLiteral("なつかしい"), QStringLiteral("adj-i")));
    // A noun carries no deconjugation class, so it is absent from the table.
    EXPECT_FALSE(table.containsTag(QStringLiteral("日"), QStringLiteral("ひ"), QStringLiteral("n")));
}

TEST(DictJmdictImporter, ProperNameEntriesOptionSkipsTheProperNameRange)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    const auto importInto = [&directory](bool properNames, const QString &fileName) {
        StoreWriter writer;
        const QString path = directory.filePath(fileName);
        EXPECT_TRUE(writer.begin(path, DictType::JMdict));
        JmdictImporter importer(properNames);
        std::atomic_bool cancel{false};
        return std::make_pair(importer.import(fixture(QStringLiteral("MockJMdict.xml")), writer, {}, cancel), path);
    };

    const auto withNames = importInto(true, QStringLiteral("with.db"));
    const auto withoutNames = importInto(false, QStringLiteral("without.db"));
    ASSERT_TRUE(withNames.first.ok);
    ASSERT_TRUE(withoutNames.first.ok);
    // The mock carries no ent_seq in 5000000 to 5999999, so both runs produce the same counts.
    EXPECT_EQ(withNames.first.recordCount, withoutNames.first.recordCount);
}

TEST(DictJmdictImporter, ReportsProgressAndHonoursCancellation)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    int lastPercent = -1;
    {
        StoreWriter writer;
        ASSERT_TRUE(writer.begin(directory.filePath(QStringLiteral("progress.db")), DictType::JMdict));
        JmdictImporter importer;
        std::atomic_bool cancel{false};
        const ImportResult result = importer.import(
            fixture(QStringLiteral("MockJMdict.xml")),
            writer,
            [&lastPercent](int percent, const QString &) {
                lastPercent = percent;
            },
            cancel);
        EXPECT_TRUE(result.ok);
    }

    StoreWriter writer;
    const QString path = directory.filePath(QStringLiteral("cancelled.db"));
    ASSERT_TRUE(writer.begin(path, DictType::JMdict));
    JmdictImporter importer;
    std::atomic_bool cancel{true};
    const ImportResult result = importer.import(fixture(QStringLiteral("MockJMdict.xml")), writer, {}, cancel);
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.cancelled);
    EXPECT_FALSE(QFile::exists(path));
}

TEST(DictJmdictImporter, ReportsAnErrorForAnAbsentSource)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());

    StoreWriter writer;
    ASSERT_TRUE(writer.begin(directory.filePath(QStringLiteral("missing.db")), DictType::JMdict));
    JmdictImporter importer;
    std::atomic_bool cancel{false};
    const ImportResult result = importer.import(directory.filePath(QStringLiteral("absent.xml")), writer, {}, cancel);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.errorString.isEmpty());
}
