// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Every record shape a store holds, and the Record variant the store hands to the lookup engine
// and the popup renderer. The shapes derive from JL (JL.Core/Dicts/**, Apache-2.0;
// see NOTICE for the pinned source): JmdictRecord.cs, JmnedictRecord.cs,
// KanjidicRecord.cs, EpwingYomichanRecord.cs, YomichanKanjiRecord.cs, PitchAccentRecord.cs,
// CustomWordRecord.cs, CustomNameRecord.cs and Freqs/FrequencyRecord.cs.
//
// Tag values are stored as the short JMdict entity names ("n", "v5r", "uk") and expanded through
// the entities map in the store's meta table at render time, as JL does with
// DictUtils.JmdictEntities.
#pragma once

#include "dict/dicttypes.h"

#include <QList>
#include <QString>

#include <optional>
#include <variant>

namespace maru::dict
{

// One image referenced by a Yomitan structured-content node or by a custom name entry. path is
// relative to the dictionary's extracted source directory; the pixel sizes come from the node
// when it carries them, and the logical sizes are the em-relative width and height Yomitan
// declares.
struct ImageInfo
{
    QString path;
    int pixelWidth = 0;
    int pixelHeight = 0;
    double width = 0.0;
    double height = 0.0;

    [[nodiscard]] bool operator==(const ImageInfo &other) const = default;
};

// Per-sense parallel tag arrays, factored into the values every sense carries and the values one
// sense adds on top. JMdict entries where all senses share one part-of-speech array are the
// common case, and storing that array once rather than N times is what
// JmdictRecordBuilder.GetExclusiveAndSharedValuesForNullableSenseField saves
// (JL.Core/Dicts/JMdict/JmdictRecordBuilder.cs).
//
// An empty perSense means every sense carries exactly sharedByAllSenses. A perSense entry that is
// empty means that sense adds nothing. When perSense is populated and sharedByAllSenses is empty,
// at least one sense had no tags at all and the split degenerates to the per-sense arrays.
struct SenseTags
{
    QList<QString> sharedByAllSenses;
    QList<QList<QString>> perSense;

    [[nodiscard]] bool isEmpty() const
    {
        return sharedByAllSenses.isEmpty() && perSense.isEmpty();
    }

    // The tags that apply to sense senseIndex: the shared array plus that sense's extras.
    [[nodiscard]] QList<QString> forSense(qsizetype senseIndex) const;

    [[nodiscard]] bool operator==(const SenseTags &other) const = default;
};

// JMdict <lsource>. language holds the English name resolved from the ISO 639-2/B code in
// xml:lang, defaulting to "English" when the attribute is absent.
struct LoanwordSource
{
    QString language;
    QString originalWord;
    bool isPart = false;
    bool isWasei = false;

    [[nodiscard]] bool operator==(const LoanwordSource &other) const = default;
};

// One JMdict headword. An entry is exploded into one record per kanji element and per reading
// element by JmdictImporter, so primarySpelling is the headword this record answers for and
// alternativeSpellings the remaining ones.
struct JmdictRecord
{
    qint32 entryId = 0; // ent_seq
    QString primarySpelling;
    QList<QString> primarySpellingOrthographyInfo; // ke_inf or re_inf entity names
    QList<QString> alternativeSpellings;
    QList<QList<QString>> alternativeSpellingsOrthographyInfo;
    QList<QString> readings;
    QList<QList<QString>> readingsOrthographyInfo;

    QList<QList<QString>> definitions;          // [sense][gloss]
    SenseTags wordClasses;                      // pos
    SenseTags fields;                           // field
    SenseTags misc;                             // misc
    SenseTags dialects;                         // dial
    QList<QList<QString>> spellingRestrictions; // stagk, per sense
    QList<QList<QString>> readingRestrictions;  // stagr, per sense
    QList<QString> definitionInfo;              // s_inf, per sense
    QList<QList<QString>> crossReferences;      // xref, prefixed "see: "/"antonym: "/"synonym: "
    QList<LoanwordSource> loanwordEtymology;
    QList<QString> info; // entry-level <info>

    // The rank derived from this headword's ke_pri or re_pri tags, in words: 500 for a headword
    // tagged nf01, 24000 for one tagged only news2, and 0 for a headword with no priority tag.
    // jmdictPriorityRank() in importers/jmdictimporter.h documents the mapping. The lookup engine
    // uses it to order results when no frequency dictionary is active.
    qint32 priorityRank = 0;

    [[nodiscard]] bool operator==(const JmdictRecord &other) const = default;
};

// One JMnedict headword. definitions and nameTypes are parallel per <trans> element.
struct JmnedictRecord
{
    qint32 entryId = 0;
    QString primarySpelling;
    QList<QString> alternativeSpellings;
    QList<QString> readings;
    QList<QList<QString>> definitions; // [translation][trans_det]
    QList<QList<QString>> nameTypes;   // [translation][name_type entity name]

    [[nodiscard]] bool operator==(const JmnedictRecord &other) const = default;
};

// One KANJIDIC2 character. frequency is the <freq> rank, 1 to 2500, and 0 when the character
// carries no <freq> element.
struct KanjidicRecord
{
    QList<QString> definitions;    // English <meaning> without m_lang
    QList<QString> onReadings;     // r_type="ja_on"
    QList<QString> kunReadings;    // r_type="ja_kun"
    QList<QString> nanoriReadings; // <nanori>
    QList<QString> radicalNames;   // <rad_name>
    quint8 strokeCount = 0;
    quint8 grade = 0;
    qint32 frequency = 0;

    [[nodiscard]] bool operator==(const KanjidicRecord &other) const = default;
};

// One Yomitan term-bank row. definitions holds the Qt rich-text rendering of each glossary
// element and definitionsPlain the same elements as plain text, for clipboard and Anki export.
// Both lists carry the same number of elements in the same order.
struct YomitanTermRecord
{
    QString primarySpelling;         // [0] expression
    QString reading;                 // [1]; empty when equal to primarySpelling
    double popularityScore = 0.0;    // [4] score
    QList<QString> definitions;      // [5] rendered to the Qt rich-text subset
    QList<QString> definitionsPlain; // [5] rendered to plain text
    QList<QString> wordClasses;      // [3] rules, whitespace split
    QList<QString> definitionTags;   // [2], whitespace split
    QList<QString> termTags;         // [7], whitespace split
    QList<ImageInfo> images;         // extracted from structured content
    qint32 sequence = 0;             // [6]

    [[nodiscard]] bool operator==(const YomitanTermRecord &other) const = default;
};

// One Yomitan kanji-bank row. stats holds the [5] object flattened to "name: value" strings.
struct YomitanKanjiRecord
{
    QList<QString> onReadings;  // [1] whitespace split
    QList<QString> kunReadings; // [2] whitespace split
    QList<QString> tags;        // [3] whitespace split
    QList<QString> definitions; // [4]
    QList<QString> stats;       // [5]

    [[nodiscard]] bool operator==(const YomitanKanjiRecord &other) const = default;
};

// One Yomitan term_meta_bank row with mode "pitch". JL keeps only positions[0]
// (JL.Core/Dicts/PitchAccent/PitchAccentRecord.cs); every position the row declares is kept
// here, in file order.
struct PitchAccentRecord
{
    QString spelling;
    QString reading; // empty when equal to spelling or absent
    QList<quint8> positions;

    [[nodiscard]] bool operator==(const PitchAccentRecord &other) const = default;
};

// One custom word-list entry. wordClasses is either the canned array the part-of-speech column
// selects or the user-supplied fifth column, which hasUserDefinedWordClass distinguishes: the
// deconjugation gate honours a user-supplied array exactly.
struct CustomWordRecord
{
    QString primarySpelling;
    QList<QString> alternativeSpellings;
    QList<QString> readings;
    QList<QString> definitions;
    QList<QString> wordClasses;
    bool hasUserDefinedWordClass = false;

    [[nodiscard]] bool operator==(const CustomWordRecord &other) const = default;
};

// One custom name-list entry.
struct CustomNameRecord
{
    QString primarySpelling;
    QString reading; // empty when equal to primarySpelling
    QString nameType;
    QString extraInfo;
    std::optional<ImageInfo> image;

    [[nodiscard]] bool operator==(const CustomNameRecord &other) const = default;
};

// One frequency-list entry. spelling holds the counterpart of the key the record is stored under:
// a record found by reading carries the spelling and a record found by spelling carries the
// reading, which is what lets frequencyFor() cross-check a hit against the headword it is
// attaching to (JL.Core/Dicts/JMdict/JmdictRecord.cs).
struct FrequencyRecord
{
    QString spelling;
    qint32 frequency = 0;

    [[nodiscard]] bool operator==(const FrequencyRecord &other) const = default;
};

// One common word written with a kanji, shown on the kanji card.
struct KanjiExample
{
    QString spelling;
    QString reading;
    QString gloss;

    [[nodiscard]] bool operator==(const KanjiExample &other) const = default;
};

// The up-to-three most common words written with kanji, built at JMdict import from the headwords
// carrying a ke_pri or re_pri priority tag. Stored in the JMdict store under the extras key
// kanjiExamplesKey(kanji), which no normalized search key can collide with.
struct KanjiExamplesRecord
{
    QString kanji;
    QList<KanjiExample> examples;

    [[nodiscard]] bool operator==(const KanjiExamplesRecord &other) const = default;
};

// The first-level IDS components of kanji, from cjkvi-ids ids.txt. Stored in the components
// dictionary under the extras key kanjiComponentsKey(kanji).
struct KanjiComponentsRecord
{
    QString kanji;
    QList<QString> components;

    [[nodiscard]] bool operator==(const KanjiComponentsRecord &other) const = default;
};

// The tag that selects the alternative held in Record::data. It is written into every payload, so
// the order is part of the on-disk format: append only. It is separate from DictType because one
// store holds more than one record kind: a JMdict store holds JmdictRecords and the
// KanjiExamplesRecords built from them.
enum class RecordKind : quint8
{
    Jmdict = 0,
    Jmnedict = 1,
    Kanjidic = 2,
    YomitanTerm = 3,
    YomitanKanji = 4,
    PitchAccent = 5,
    CustomWord = 6,
    CustomName = 7,
    Frequency = 8,
    KanjiExamples = 9,
    KanjiComponents = 10,
};

using RecordData = std::variant<JmdictRecord,
                                JmnedictRecord,
                                KanjidicRecord,
                                YomitanTermRecord,
                                YomitanKanjiRecord,
                                PitchAccentRecord,
                                CustomWordRecord,
                                CustomNameRecord,
                                FrequencyRecord,
                                KanjiExamplesRecord,
                                KanjiComponentsRecord>;

// One decoded row of a store's record table. id is the row id the search_key table points at, and
// type the store's dictionary type, which selects the render path in popup/.
struct Record
{
    qint64 id = 0;
    DictType type = DictType::JMdict;
    RecordData data;

    [[nodiscard]] RecordKind kind() const
    {
        return static_cast<RecordKind>(data.index());
    }
};

// Typed accessors. Each returns nullptr when the record holds a different alternative.
[[nodiscard]] const JmdictRecord *asJmdict(const Record &record);
[[nodiscard]] const JmnedictRecord *asJmnedict(const Record &record);
[[nodiscard]] const KanjidicRecord *asKanjidic(const Record &record);
[[nodiscard]] const YomitanTermRecord *asYomitanTerm(const Record &record);
[[nodiscard]] const YomitanKanjiRecord *asYomitanKanji(const Record &record);
[[nodiscard]] const PitchAccentRecord *asPitchAccent(const Record &record);
[[nodiscard]] const CustomWordRecord *asCustomWord(const Record &record);
[[nodiscard]] const CustomNameRecord *asCustomName(const Record &record);
[[nodiscard]] const FrequencyRecord *asFrequency(const Record &record);
[[nodiscard]] const KanjiExamplesRecord *asKanjiExamples(const Record &record);
[[nodiscard]] const KanjiComponentsRecord *asKanjiComponents(const Record &record);

// The headword the record answers for, across every kind that has one. Empty for the frequency,
// examples and components kinds.
[[nodiscard]] QString primarySpelling(const Record &record);

// The readings the record declares, across every kind that has them. A record whose reading field
// is a single string returns a one-element list, and an empty reading returns an empty list.
[[nodiscard]] QList<QString> readings(const Record &record);

// The alternative spellings the record declares, empty for the kinds that have none.
[[nodiscard]] QList<QString> alternativeSpellings(const Record &record);

// The deconjugation word classes the record declares. JMdict returns the union across senses,
// Yomitan terms the [3] rules array and custom words their word-class array. Empty for every
// other kind.
[[nodiscard]] QList<QString> wordClasses(const Record &record);

// U+0001, which maru::jp::normalizeText() never emits, so a key starting with it cannot collide
// with a search key an importer writes.
inline constexpr QChar extrasKeyPrefix = QChar(u'\x0001');

// The key the kanji-examples record for kanji is stored under.
[[nodiscard]] QString kanjiExamplesKey(QStringView kanji);

// The key the kanji-components record for kanji is stored under.
[[nodiscard]] QString kanjiComponentsKey(QStringView kanji);

} // namespace maru::dict
