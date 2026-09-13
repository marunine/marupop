// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Round trips every record kind through the CBOR payload codec, which is the check that the
// encoder and the decoder read the same field order.
#include "dict/codec.h"
#include "dict/records.h"

#include <gtest/gtest.h>

using namespace maru::dict;

namespace
{

Record roundTrip(const Record &record)
{
    const QByteArray payload = encodeRecord(record);
    EXPECT_FALSE(payload.isEmpty());
    const std::optional<Record> decoded = decodeRecord(record.type, payload);
    EXPECT_TRUE(decoded.has_value());
    return decoded.value_or(Record{});
}

JmdictRecord fullJmdictRecord()
{
    JmdictRecord record;
    record.entryId = 1472050;
    record.primarySpelling = QStringLiteral("廃墟");
    record.primarySpellingOrthographyInfo = {QStringLiteral("oK")};
    record.alternativeSpellings = {QStringLiteral("廃虚")};
    record.alternativeSpellingsOrthographyInfo = {{QStringLiteral("rK")}};
    record.readings = {QStringLiteral("はいきょ")};
    record.readingsOrthographyInfo = {{QStringLiteral("ik")}};
    record.definitions = {{QStringLiteral("ruins"), QStringLiteral("remains")}, {QStringLiteral("abandoned building")}};
    record.wordClasses.sharedByAllSenses = {QStringLiteral("n")};
    record.wordClasses.perSense = {{}, {QStringLiteral("adj-no")}};
    record.fields.sharedByAllSenses = {QStringLiteral("archit")};
    record.misc.perSense = {{QStringLiteral("uk")}, {}};
    record.dialects.sharedByAllSenses = {QStringLiteral("ksb")};
    record.spellingRestrictions = {{QStringLiteral("廃墟")}, {}};
    record.readingRestrictions = {{}, {QStringLiteral("はいきょ")}};
    record.definitionInfo = {QStringLiteral("usually written in kana"), QString()};
    record.crossReferences = {{QStringLiteral("see: 遺跡")}, {}};
    record.loanwordEtymology = {LoanwordSource{.language = QStringLiteral("German"),
                                               .originalWord = QStringLiteral("Ruine"),
                                               .isPart = true,
                                               .isWasei = false}};
    record.info = {QStringLiteral("entry level info")};
    record.priorityRank = 11000;
    return record;
}

} // namespace

TEST(DictCodec, JmdictRecordRoundTrips)
{
    Record record;
    record.type = DictType::JMdict;
    record.data = fullJmdictRecord();

    const Record decoded = roundTrip(record);
    ASSERT_EQ(decoded.kind(), RecordKind::Jmdict);
    ASSERT_NE(asJmdict(decoded), nullptr);
    EXPECT_EQ(*asJmdict(decoded), fullJmdictRecord());
    EXPECT_EQ(decoded.type, DictType::JMdict);
}

TEST(DictCodec, EmptyJmdictRecordRoundTrips)
{
    Record record;
    record.type = DictType::JMdict;
    record.data = JmdictRecord{};

    const Record decoded = roundTrip(record);
    ASSERT_NE(asJmdict(decoded), nullptr);
    EXPECT_EQ(*asJmdict(decoded), JmdictRecord{});
}

TEST(DictCodec, JmnedictRecordRoundTrips)
{
    JmnedictRecord source;
    source.entryId = 5000001;
    source.primarySpelling = QStringLiteral("田中");
    source.alternativeSpellings = {QStringLiteral("多中")};
    source.readings = {QStringLiteral("たなか")};
    source.definitions = {{QStringLiteral("Tanaka")}};
    source.nameTypes = {{QStringLiteral("surname")}};

    Record record;
    record.type = DictType::JMnedict;
    record.data = source;
    EXPECT_EQ(*asJmnedict(roundTrip(record)), source);
}

TEST(DictCodec, KanjidicRecordRoundTrips)
{
    KanjidicRecord source;
    source.definitions = {QStringLiteral("day"), QStringLiteral("sun")};
    source.onReadings = {QStringLiteral("ニチ")};
    source.kunReadings = {QStringLiteral("ひ")};
    source.nanoriReadings = {QStringLiteral("あき")};
    source.radicalNames = {QStringLiteral("いち")};
    source.strokeCount = 4;
    source.grade = 1;
    source.frequency = 1;

    Record record;
    record.type = DictType::Kanjidic;
    record.data = source;
    EXPECT_EQ(*asKanjidic(roundTrip(record)), source);
}

TEST(DictCodec, YomitanTermRecordRoundTrips)
{
    YomitanTermRecord source;
    source.primarySpelling = QStringLiteral("走る");
    source.reading = QStringLiteral("はしる");
    source.popularityScore = 42.5;
    source.definitions = {QStringLiteral("<b>to run</b>")};
    source.definitionsPlain = {QStringLiteral("to run")};
    source.wordClasses = {QStringLiteral("v5r")};
    source.definitionTags = {QStringLiteral("v5r"), QStringLiteral("vs")};
    source.termTags = {QStringLiteral("common")};
    source.images = {ImageInfo{
        .path = QStringLiteral("img/real.png"), .pixelWidth = 120, .pixelHeight = 80, .width = 1.5, .height = 1.0}};
    source.sequence = 7;

    Record record;
    record.type = DictType::YomitanWord;
    record.data = source;
    EXPECT_EQ(*asYomitanTerm(roundTrip(record)), source);
}

TEST(DictCodec, YomitanKanjiRecordRoundTrips)
{
    YomitanKanjiRecord source;
    source.onReadings = {QStringLiteral("ニチ")};
    source.kunReadings = {QStringLiteral("ひ")};
    source.tags = {QStringLiteral("jouyou")};
    source.definitions = {QStringLiteral("day")};
    source.stats = {QStringLiteral("strokes: 4")};

    Record record;
    record.type = DictType::YomitanKanji;
    record.data = source;
    EXPECT_EQ(*asYomitanKanji(roundTrip(record)), source);
}

TEST(DictCodec, PitchAccentRecordRoundTrips)
{
    PitchAccentRecord source;
    source.spelling = QStringLiteral("橋");
    source.reading = QStringLiteral("はし");
    source.positions = {2, 0};

    Record record;
    record.type = DictType::YomitanPitchAccent;
    record.data = source;
    EXPECT_EQ(*asPitchAccent(roundTrip(record)), source);
}

TEST(DictCodec, CustomRecordsRoundTrip)
{
    CustomWordRecord word;
    word.primarySpelling = QStringLiteral("走る");
    word.alternativeSpellings = {QStringLiteral("奔る")};
    word.readings = {QStringLiteral("はしる")};
    word.definitions = {QStringLiteral("to run")};
    word.wordClasses = {QStringLiteral("v5r")};
    word.hasUserDefinedWordClass = true;

    Record wordRecord;
    wordRecord.type = DictType::CustomWord;
    wordRecord.data = word;
    EXPECT_EQ(*asCustomWord(roundTrip(wordRecord)), word);

    CustomNameRecord name;
    name.primarySpelling = QStringLiteral("田中太郎");
    name.reading = QStringLiteral("たなかたろう");
    name.nameType = QStringLiteral("Person");
    name.extraInfo = QStringLiteral("line one\nline two");
    name.image = ImageInfo{
        .path = QStringLiteral("faces/tanaka.png"), .pixelWidth = 64, .pixelHeight = 64, .width = 0, .height = 0};

    Record nameRecord;
    nameRecord.type = DictType::CustomName;
    nameRecord.data = name;
    EXPECT_EQ(*asCustomName(roundTrip(nameRecord)), name);

    CustomNameRecord withoutImage = name;
    withoutImage.image.reset();
    nameRecord.data = withoutImage;
    const Record decoded = roundTrip(nameRecord);
    ASSERT_NE(asCustomName(decoded), nullptr);
    EXPECT_FALSE(asCustomName(decoded)->image.has_value());
}

TEST(DictCodec, FrequencyAndExtrasRoundTrip)
{
    FrequencyRecord frequency{.spelling = QStringLiteral("はしる"), .frequency = 1234};
    Record frequencyRecord;
    frequencyRecord.type = DictType::YomitanFrequency;
    frequencyRecord.data = frequency;
    EXPECT_EQ(*asFrequency(roundTrip(frequencyRecord)), frequency);

    KanjiExamplesRecord examples;
    examples.kanji = QStringLiteral("日");
    examples.examples = {KanjiExample{.spelling = QStringLiteral("日本"),
                                      .reading = QStringLiteral("にほん"),
                                      .gloss = QStringLiteral("Japan")},
                         KanjiExample{.spelling = QStringLiteral("毎日"),
                                      .reading = QStringLiteral("まいにち"),
                                      .gloss = QStringLiteral("every day")}};
    Record examplesRecord;
    examplesRecord.type = DictType::JMdict;
    examplesRecord.data = examples;
    EXPECT_EQ(*asKanjiExamples(roundTrip(examplesRecord)), examples);

    KanjiComponentsRecord components;
    components.kanji = QStringLiteral("清");
    components.components = {QStringLiteral("氵"), QStringLiteral("青")};
    Record componentsRecord;
    componentsRecord.type = DictType::KanjiComponents;
    componentsRecord.data = components;
    EXPECT_EQ(*asKanjiComponents(roundTrip(componentsRecord)), components);
}

TEST(DictCodec, PayloadKindIsReadableWithoutDecoding)
{
    Record record;
    record.type = DictType::JMdict;
    record.data = fullJmdictRecord();
    const QByteArray payload = encodeRecord(record);

    const std::optional<RecordKind> kind = payloadKind(payload);
    ASSERT_TRUE(kind.has_value());
    EXPECT_EQ(*kind, RecordKind::Jmdict);
}

TEST(DictCodec, TruncatedPayloadIsRejected)
{
    Record record;
    record.type = DictType::JMdict;
    record.data = fullJmdictRecord();
    const QByteArray payload = encodeRecord(record);

    EXPECT_FALSE(decodeRecord(DictType::JMdict, payload.left(payload.size() / 2)).has_value());
    EXPECT_FALSE(decodeRecord(DictType::JMdict, QByteArray()).has_value());
    EXPECT_FALSE(decodeRecord(DictType::JMdict, QByteArray("not cbor at all")).has_value());
}

TEST(DictCodec, AccessorsReturnNullForOtherKinds)
{
    Record record;
    record.type = DictType::JMdict;
    record.data = fullJmdictRecord();

    EXPECT_NE(asJmdict(record), nullptr);
    EXPECT_EQ(asJmnedict(record), nullptr);
    EXPECT_EQ(asKanjidic(record), nullptr);
    EXPECT_EQ(primarySpelling(record), QStringLiteral("廃墟"));
    EXPECT_EQ(readings(record), QList<QString>{QStringLiteral("はいきょ")});
    EXPECT_EQ(alternativeSpellings(record), QList<QString>{QStringLiteral("廃虚")});
    EXPECT_EQ(wordClasses(record), (QList<QString>{QStringLiteral("n"), QStringLiteral("adj-no")}));
}

TEST(DictCodec, SenseTagsExpandPerSense)
{
    SenseTags tags;
    tags.sharedByAllSenses = {QStringLiteral("n")};
    tags.perSense = {{}, {QStringLiteral("adj-no")}};

    EXPECT_EQ(tags.forSense(0), QList<QString>{QStringLiteral("n")});
    EXPECT_EQ(tags.forSense(1), (QList<QString>{QStringLiteral("n"), QStringLiteral("adj-no")}));
    EXPECT_EQ(tags.forSense(5), QList<QString>{QStringLiteral("n")});
}
