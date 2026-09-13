// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// KANJIDIC2, JMnedict, the custom TSV lists and cjkvi-ids, over the hand-written fixtures in
// tests/data/dict.
#include "dict/importers/customnameimporter.h"
#include "dict/importers/customwordimporter.h"
#include "dict/importers/idsimporter.h"
#include "dict/importers/jmnedictimporter.h"
#include "dict/importers/kanjidicimporter.h"
#include "dict/keynorm.h"
#include "dict/records.h"
#include "dict/store.h"

#include <QFile>
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

ImportResult runImport(Importer &importer, const QString &source, const QString &path, DictType type)
{
    StoreWriter writer;
    if (!writer.begin(path, type))
        return {};
    std::atomic_bool cancel{false};
    return importer.import(source, writer, {}, cancel);
}

} // namespace

TEST(DictKanjidicImporter, ImportsThreeCharacters)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("kanjidic.db"));

    KanjidicImporter importer;
    const ImportResult result =
        runImport(importer, fixture(QStringLiteral("mock_kanjidic2.xml")), path, DictType::Kanjidic);
    ASSERT_TRUE(result.ok) << result.errorString.toStdString();
    EXPECT_EQ(result.recordCount, 3);
    EXPECT_EQ(result.keyCount, 3);
    EXPECT_EQ(result.maxKeyLength, 1);

    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);

    const auto sun = store.find(QStringLiteral("日"));
    ASSERT_EQ(sun.size(), 1U);
    const KanjidicRecord *record = asKanjidic(*sun.front());
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->strokeCount, 4);
    EXPECT_EQ(record->grade, 1);
    EXPECT_EQ(record->frequency, 1);
    EXPECT_EQ(record->onReadings, (QList<QString>{QStringLiteral("ニチ"), QStringLiteral("ジツ")}));
    EXPECT_EQ(record->kunReadings.size(), 3);
    // A <meaning> with an m_lang attribute is a translation into another language and is skipped.
    EXPECT_EQ(record->definitions,
              (QList<QString>{QStringLiteral("day"), QStringLiteral("sun"), QStringLiteral("Japan")}));
    EXPECT_EQ(record->nanoriReadings, (QList<QString>{QStringLiteral("あ"), QStringLiteral("あき")}));

    const auto one = store.find(QStringLiteral("一"));
    ASSERT_EQ(one.size(), 1U);
    EXPECT_EQ(asKanjidic(*one.front())->radicalNames, QList<QString>{QStringLiteral("いち")});

    // The key is the raw literal, not a normalized one, and a kana key finds nothing.
    EXPECT_TRUE(store.find(QStringLiteral("ひ")).empty());
}

TEST(DictJmnedictImporter, KeysKanjiFormsAndFallsBackToReadings)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("jmnedict.db"));

    JmnedictImporter importer;
    const ImportResult result =
        runImport(importer, fixture(QStringLiteral("mock_jmnedict.xml")), path, DictType::JMnedict);
    ASSERT_TRUE(result.ok) << result.errorString.toStdString();
    EXPECT_EQ(result.recordCount, 4); // 田中, 多中, さくら, 東京

    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);

    const auto tanaka = store.find(normalizeKey(QStringLiteral("田中")));
    ASSERT_EQ(tanaka.size(), 1U);
    const JmnedictRecord *record = asJmnedict(*tanaka.front());
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->entryId, 5000001);
    EXPECT_EQ(record->primarySpelling, QStringLiteral("田中"));
    EXPECT_EQ(record->alternativeSpellings, QList<QString>{QStringLiteral("多中")});
    EXPECT_EQ(record->readings, QList<QString>{QStringLiteral("たなか")});
    EXPECT_EQ(record->definitions, QList<QList<QString>>{QList<QString>{QStringLiteral("Tanaka")}});
    EXPECT_EQ(record->nameTypes, QList<QList<QString>>{QList<QString>{QStringLiteral("surname")}});

    // An entry with kanji forms is not reachable by its reading.
    EXPECT_TRUE(store.find(normalizeKey(QStringLiteral("たなか"))).empty());

    // An entry with no kanji form is keyed by its reading, and carries no readings of its own.
    const auto sakura = store.find(normalizeKey(QStringLiteral("さくら")));
    ASSERT_EQ(sakura.size(), 1U);
    const JmnedictRecord *sakuraRecord = asJmnedict(*sakura.front());
    ASSERT_NE(sakuraRecord, nullptr);
    EXPECT_EQ(sakuraRecord->primarySpelling, QStringLiteral("さくら"));
    EXPECT_TRUE(sakuraRecord->readings.isEmpty());
    EXPECT_EQ(sakuraRecord->definitions.size(), 2);
    EXPECT_EQ(sakuraRecord->nameTypes.size(), 2);

    // The entities map keeps the short names and their descriptions.
    EXPECT_EQ(importer.entities().value(QStringLiteral("surname")), QStringLiteral("family or surname"));
}

TEST(DictCustomWordImporter, ParsesTheTabSeparatedFormat)
{
    const CustomWordImporter::ParsedLine verb =
        CustomWordImporter::parseLine(QStringLiteral("走る;奔る\tはしる\tto run;to dash\tVerb"));
    ASSERT_TRUE(verb.ok);
    ASSERT_EQ(verb.records.size(), 2);
    EXPECT_EQ(verb.records.at(0).primarySpelling, QStringLiteral("走る"));
    EXPECT_EQ(verb.records.at(0).alternativeSpellings, QList<QString>{QStringLiteral("奔る")});
    EXPECT_EQ(verb.records.at(0).definitions, (QList<QString>{QStringLiteral("to run"), QStringLiteral("to dash")}));
    EXPECT_EQ(verb.records.at(0).wordClasses.size(), 21);
    EXPECT_TRUE(verb.records.at(0).wordClasses.contains(QStringLiteral("v5r")));
    EXPECT_FALSE(verb.records.at(0).hasUserDefinedWordClass);
    // The reading keys attach to the first spelling alone.
    EXPECT_EQ(verb.keys.at(0), (QList<QString>{QStringLiteral("走る"), QStringLiteral("はしる")}));
    EXPECT_EQ(verb.keys.at(1), QList<QString>{QStringLiteral("奔る")});

    const CustomWordImporter::ParsedLine adjective =
        CustomWordImporter::parseLine(QStringLiteral("凄い\tすごい\tamazing\tAdjective\tadj-i"));
    ASSERT_TRUE(adjective.ok);
    EXPECT_EQ(adjective.records.at(0).wordClasses, QList<QString>{QStringLiteral("adj-i")});
    EXPECT_TRUE(adjective.records.at(0).hasUserDefinedWordClass);

    // A single spelling equal to its single reading carries no reading.
    const CustomWordImporter::ParsedLine kana = CustomWordImporter::parseLine(QStringLiteral("ねこ\tねこ\tcat\tNoun"));
    ASSERT_TRUE(kana.ok);
    EXPECT_TRUE(kana.records.at(0).readings.isEmpty());
    EXPECT_EQ(kana.records.at(0).wordClasses, QList<QString>{QStringLiteral("n")});

    EXPECT_FALSE(CustomWordImporter::parseLine(QStringLiteral("too\tfew\tfields")).ok);
    EXPECT_FALSE(CustomWordImporter::parseLine(QString()).ok);
}

TEST(DictCustomWordImporter, ImportsTheFixtureAndAppendsAnEntry)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("customwords.db"));

    CustomWordImporter importer;
    const ImportResult result =
        runImport(importer, fixture(QStringLiteral("custom_words.txt")), path, DictType::CustomWord);
    ASSERT_TRUE(result.ok) << result.errorString.toStdString();
    EXPECT_EQ(result.recordCount, 4); // 走る, 奔る, テスト単語, 凄い

    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);
    const auto records = store.find(normalizeKey(QStringLiteral("はしる")));
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(asCustomWord(*records.front())->primarySpelling, QStringLiteral("走る"));

    // A literal \n in the definitions column becomes a newline.
    const auto multiline = store.find(normalizeKey(QStringLiteral("テスト単語")));
    ASSERT_EQ(multiline.size(), 1U);
    ASSERT_FALSE(asCustomWord(*multiline.front())->definitions.isEmpty());
    EXPECT_TRUE(asCustomWord(*multiline.front())->definitions.first().contains(QLatin1Char('\n')));

    const QString listPath = directory.filePath(QStringLiteral("appended.txt"));
    ASSERT_TRUE(CustomWordImporter::appendEntry(listPath,
                                                {QStringLiteral("追加")},
                                                {QStringLiteral("ついか")},
                                                {QStringLiteral("an addition")},
                                                QStringLiteral("Noun"),
                                                {}));
    QFile appended(listPath);
    ASSERT_TRUE(appended.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString line = QString::fromUtf8(appended.readAll()).trimmed();
    EXPECT_EQ(line, QStringLiteral("追加\tついか\tan addition\tNoun"));
    const CustomWordImporter::ParsedLine reparsed = CustomWordImporter::parseLine(line);
    ASSERT_TRUE(reparsed.ok);
    EXPECT_EQ(reparsed.records.at(0).primarySpelling, QStringLiteral("追加"));
}

TEST(DictCustomNameImporter, ParsesAndImportsTheNameList)
{
    const std::optional<CustomNameRecord> record =
        CustomNameImporter::parseLine(QStringLiteral("田中太郎\tたなかたろう\tPerson\tline\\ntwo\tface.png"));
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->primarySpelling, QStringLiteral("田中太郎"));
    EXPECT_EQ(record->reading, QStringLiteral("たなかたろう"));
    EXPECT_EQ(record->nameType, QStringLiteral("Person"));
    EXPECT_EQ(record->extraInfo, QStringLiteral("line\ntwo"));
    ASSERT_TRUE(record->image.has_value());
    EXPECT_EQ(record->image->path, QStringLiteral("face.png"));

    // A reading equal to the spelling is dropped.
    const std::optional<CustomNameRecord> sameReading =
        CustomNameImporter::parseLine(QStringLiteral("さくら\tさくら\tPerson"));
    ASSERT_TRUE(sameReading.has_value());
    EXPECT_TRUE(sameReading->reading.isEmpty());

    EXPECT_FALSE(CustomNameImporter::parseLine(QStringLiteral("only\ttwo")).has_value());

    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("customnames.db"));

    CustomNameImporter importer;
    const ImportResult result =
        runImport(importer, fixture(QStringLiteral("custom_names.txt")), path, DictType::CustomName);
    ASSERT_TRUE(result.ok) << result.errorString.toStdString();
    EXPECT_EQ(result.recordCount, 2);

    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);
    // Only the spelling is a key.
    EXPECT_EQ(store.find(normalizeKey(QStringLiteral("田中太郎"))).size(), 1U);
    EXPECT_TRUE(store.find(normalizeKey(QStringLiteral("たなかたろう"))).empty());
}

TEST(DictIdsImporter, ParsesTheDescriptionSequences)
{
    QString kanji;
    // A character whose sequence is itself has no components.
    EXPECT_TRUE(IdsImporter::parseComponents(QStringLiteral("U+65E5\t日\t日"), &kanji).isEmpty());
    EXPECT_EQ(kanji, QStringLiteral("日"));

    EXPECT_EQ(IdsImporter::parseComponents(QStringLiteral("U+6E05\t清\t⿰氵青"), &kanji),
              (QList<QString>{QStringLiteral("氵"), QStringLiteral("青")}));
    EXPECT_EQ(kanji, QStringLiteral("清"));

    // The first sequence is read and its region tag dropped.
    EXPECT_EQ(IdsImporter::parseComponents(QStringLiteral("U+4E38\t丸\t⿻九丶[GJ]\t⿵九丶[TKV]"), &kanji),
              (QList<QString>{QStringLiteral("九"), QStringLiteral("丶")}));

    EXPECT_TRUE(IdsImporter::parseComponents(QStringLiteral("# a comment"), &kanji).isEmpty());
    EXPECT_TRUE(IdsImporter::parseComponents(QStringLiteral("U+0001\tx"), &kanji).isEmpty());
}

TEST(DictIdsImporter, ImportsTheComponentList)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("ids.db"));

    IdsImporter importer;
    const ImportResult result =
        runImport(importer, fixture(QStringLiteral("mock_ids.txt")), path, DictType::KanjiComponents);
    ASSERT_TRUE(result.ok) << result.errorString.toStdString();
    // 日 has no components of its own, so three of the four lines produce a record.
    EXPECT_EQ(result.recordCount, 3);

    Store store;
    ASSERT_EQ(store.open(path), OpenResult::Ok);
    const auto records = store.find(kanjiComponentsKey(QStringLiteral("語")));
    ASSERT_EQ(records.size(), 1U);
    const KanjiComponentsRecord *record = asKanjiComponents(*records.front());
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->components, (QList<QString>{QStringLiteral("言"), QStringLiteral("吾")}));
    EXPECT_TRUE(store.find(kanjiComponentsKey(QStringLiteral("日"))).empty());
}
