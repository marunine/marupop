// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The lookup engine over a hermetic dictionary set built in a temporary directory: JL's mock
// JMdict, a supplementary mock of our own, a mock JMnedict, a mock KANJIDIC2, the cjkvi-ids
// fixture, and hand-written Yomitan frequency and pitch-accent dictionaries.
//
// The first thirteen tests are a port of JL's LookupTests (JL.Core.Tests/LookupTests.cs,
// Apache-2.0): the field-by-field
// shape of 始まる and the per-spelling and per-reading frequency resolution, including the two
// cases where the frequency has to come back absent rather than zero. JL reads its built-in
// Nazeka frequency lists; the fixture under tests/data/lookup/freq carries the same ranks in the
// Yomitan reading-object shape, which is the one marupop imports.
#include "core/enums.h"
#include "deconj/deconjugator.h"
#include "dict/dictionary.h"
#include "dict/dictionarymanager.h"
#include "dict/importers/idsimporter.h"
#include "dict/importers/importer.h"
#include "dict/importers/jmdictimporter.h"
#include "dict/importers/jmnedictimporter.h"
#include "dict/importers/kanjidicimporter.h"
#include "dict/importers/yomitanimporter.h"
#include "dict/lookupsupport.h"
#include "dict/records.h"
#include "dict/store.h"
#include "jp/japanese.h"
#include "lookup/decorate.h"
#include "lookup/engine.h"
#include "lookup/format.h"
#include "lookup/popupadapter.h"
#include "lookup/query.h"
#include "lookup/ranking.h"
#include "lookup/textinfo.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QTemporaryDir>
#include <QUuid>

#include <atomic>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <thread>

using namespace Qt::Literals::StringLiterals;
using namespace maru;
using namespace maru::lookup;
using maru::dict::DictType;

namespace
{

QString dictFixture(const QString &name)
{
    return QStringLiteral(MARUPOP_DICT_TEST_DATA_DIR) + QLatin1Char('/') + name;
}

QString lookupFixture(const QString &name)
{
    return QStringLiteral(MARUPOP_LOOKUP_TEST_DATA_DIR) + QLatin1Char('/') + name;
}

// The whole dictionary set of the suite, imported once and kept for the process. Importing the
// six mock sources takes a few milliseconds; doing it per test would still be cheap, but every
// test then needs its own temporary directory and the engine's cache would never be exercised
// across tests.
class Fixture
{
public:
    Fixture()
    {
        EXPECT_TRUE(m_directory.isValid());
        m_manager = std::make_unique<dict::DictionaryManager>(m_directory.path(), nullptr);

        jmdict = import(DictType::JMdict, QStringLiteral("JMdict"), dictFixture(u"MockJMdict.xml"_s), 1);
        extra = import(DictType::JMdict, QStringLiteral("JMdict Extra"), lookupFixture(u"mock_extra.xml"_s), 2);
        names = import(DictType::JMnedict, QStringLiteral("JMnedict"), dictFixture(u"mock_jmnedict.xml"_s), 3);
        kanji = import(DictType::Kanjidic, QStringLiteral("KANJIDIC2"), dictFixture(u"mock_kanjidic2.xml"_s), 4);
        components = import(DictType::KanjiComponents, QStringLiteral("Components"), dictFixture(u"mock_ids.txt"_s), 5);
        frequency = import(DictType::YomitanFrequency, QStringLiteral("Mock Frequency"), lookupFixture(u"freq"_s), 6);
        pitch = import(DictType::YomitanPitchAccent, QStringLiteral("Mock Pitch"), lookupFixture(u"pitch"_s), 7);

        // JL's test sets NewlineBetweenDefinitions to false, which is what makes the expected
        // definition string use the fullwidth semicolon.
        for (const QUuid &id : {jmdict, extra}) {
            dict::DictOptions options = m_manager->dictionary(id)->options;
            options.newlineBetweenDefinitions = false;
            EXPECT_TRUE(m_manager->setOptions(id, options));
        }

        m_manager->openAll();
        EXPECT_TRUE(m_manager->loadWordClassTable());

        std::optional<deconj::RuleSet> rules = deconj::RuleSet::loadEmbedded();
        EXPECT_TRUE(rules.has_value());
        m_rules = std::move(rules);
        m_engine = std::make_unique<Engine>(*m_manager, *m_rules);
    }

    [[nodiscard]] dict::DictionaryManager &manager()
    {
        return *m_manager;
    }

    // What Application does on dict::DictionaryManager::changed(): pushes the republished
    // dictionary set into the engine, which drops the results the previous set produced.
    void refreshEngine()
    {
        m_engine->setDictionaries(m_manager->snapshot(), m_manager->wordClasses());
    }

    // The handle of one fixture dictionary in the currently published snapshot.
    [[nodiscard]] dict::DictionaryHandle handle(const QUuid &id)
    {
        const dict::DictionaryHandle *found = dict::handleFor(m_manager->snapshot(), id);
        EXPECT_NE(found, nullptr);
        return found == nullptr ? dict::DictionaryHandle{} : *found;
    }

    [[nodiscard]] Engine &engine()
    {
        return *m_engine;
    }

    [[nodiscard]] const deconj::RuleSet &rules() const
    {
        return *m_rules;
    }

    // One lookup of text with the cursor at its start, which is what every ported JL test does.
    [[nodiscard]] Response lookup(const QString &text,
                                  LookupCategory category = LookupCategory::All,
                                  int maxResults = 10,
                                  qsizetype cursorIndex = 0)
    {
        Request request;
        request.sourceText = text;
        request.cursorIndex = cursorIndex;
        request.category = category;
        request.maxResults = maxResults;
        return m_engine->lookup(request);
    }

    QUuid jmdict;
    QUuid extra;
    QUuid names;
    QUuid kanji;
    QUuid components;
    QUuid frequency;
    QUuid pitch;

private:
    QUuid import(DictType type, const QString &name, const QString &source, int priority)
    {
        dict::Dictionary entry;
        entry.type = type;
        entry.name = name;
        entry.sourcePath = source;
        entry.priority = priority;
        dict::Dictionary *stored = m_manager->add(entry);
        EXPECT_NE(stored, nullptr);
        if (stored == nullptr)
            return {};

        std::unique_ptr<dict::Importer> importer;
        switch (type) {
        case DictType::JMdict:
            importer = std::make_unique<dict::JmdictImporter>(true);
            break;
        case DictType::JMnedict:
            importer = std::make_unique<dict::JmnedictImporter>();
            break;
        case DictType::Kanjidic:
            importer = std::make_unique<dict::KanjidicImporter>();
            break;
        case DictType::KanjiComponents:
            importer = std::make_unique<dict::IdsImporter>();
            break;
        default:
            importer = std::make_unique<dict::YomitanImporter>(type);
            break;
        }

        dict::StoreWriter writer;
        EXPECT_TRUE(writer.begin(dict::databasePathFor(m_directory.path(), stored->id), type));
        std::atomic_bool cancel{false};
        const dict::ImportResult result = importer->import(source, writer, [](int, const QString &) {}, cancel);
        EXPECT_TRUE(result.ok) << name.toStdString() << ": " << result.errorString.toStdString();

        stored->recordCount = result.recordCount;
        stored->keyCount = result.keyCount;
        stored->maxKeyLength = result.maxKeyLength;
        stored->importedAt = QDateTime::currentDateTimeUtc();

        if (auto *jmdictImporter = dynamic_cast<dict::JmdictImporter *>(importer.get())) {
            EXPECT_TRUE(
                jmdictImporter->wordClassTable().save(dict::wordClassTablePathFor(m_directory.path(), stored->id)));
        }
        return stored->id;
    }

    QTemporaryDir m_directory;
    std::unique_ptr<dict::DictionaryManager> m_manager;
    std::optional<deconj::RuleSet> m_rules;
    std::unique_ptr<Engine> m_engine;
};

Fixture &fixture()
{
    static Fixture instance;
    return instance;
}

// The first result whose primary spelling is spelling, or a default Result.
Result resultForSpelling(const Response &response, QStringView spelling)
{
    for (const Result &result : response.results) {
        if (result.primarySpelling == spelling)
            return result;
    }
    return {};
}

// The first result carrying reading among its readings, which is how JL's frequency tests select
// the entry they mean.
Result resultForReading(const Response &response, const QString &reading)
{
    for (const Result &result : response.results) {
        if (result.readings.contains(reading))
            return result;
    }
    return {};
}

// The rank of the highest-priority frequency dictionary, or INT_MAX when the headword is not
// covered. JL returns int.MaxValue for both an absent list and an absent entry.
int frequencyOf(const Result &result)
{
    return result.frequencies.isEmpty() ? std::numeric_limits<int>::max() : result.frequencies.first().rank;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// JL LookupTests, ported one for one.
// ---------------------------------------------------------------------------------------------

TEST(LookupJL, ShapeOf始まる)
{
    const Response response = fixture().lookup(u"始まる"_s);
    ASSERT_FALSE(response.results.isEmpty());
    const Result &result = response.results.first();

    EXPECT_EQ(result.matchedText, u"始まる"_s);
    EXPECT_EQ(result.dictionary.name, u"JMdict"_s);
    ASSERT_EQ(result.frequencies.size(), 1);
    EXPECT_EQ(result.frequencies.first().dictionaryName, u"Mock Frequency"_s);
    EXPECT_EQ(result.frequencies.first().rank, 759);
    EXPECT_FALSE(result.frequencies.first().higherIsBetter);
    EXPECT_EQ(result.primarySpelling, u"始まる"_s);
    // JL reports null here: the result was matched on its own headword, without deconjugation.
    EXPECT_TRUE(result.deconjugatedMatchedText.isEmpty());
    EXPECT_EQ(result.readings, QList<QString>{u"はじまる"_s});
    EXPECT_EQ(formatDefinitions(result),
              u"[v5r, vi] 1. to begin; to start; to commence；2. to happen (again); to begin (anew)；"
              "3. to date (from); to originate (in)"_s);
    EXPECT_EQ(entryIdOf(result), 1307500);

    const dict::JmdictRecord *record = dict::asJmdict(*result.record);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->wordClasses.sharedByAllSenses, (QList<QString>{u"v5r"_s, u"vi"_s}));
}

TEST(LookupJL, Frequency他)
{
    EXPECT_EQ(frequencyOf(resultForSpelling(fixture().lookup(u"た"_s), u"他"_s)), 294);
}

TEST(LookupJL, Frequency多)
{
    EXPECT_EQ(frequencyOf(resultForSpelling(fixture().lookup(u"た"_s), u"多"_s)), 9844);
}

TEST(LookupJL, Frequency田)
{
    EXPECT_EQ(frequencyOf(resultForSpelling(fixture().lookup(u"た"_s), u"田"_s)), 21431);
}

TEST(LookupJL, Frequency日ひ)
{
    EXPECT_EQ(frequencyOf(resultForReading(fixture().lookup(u"日"_s), u"ひ"_s)), 227);
}

TEST(LookupJL, Frequency日にち)
{
    EXPECT_EQ(frequencyOf(resultForReading(fixture().lookup(u"日"_s), u"にち"_s)), 777);
}

TEST(LookupJL, Frequency日か)
{
    EXPECT_EQ(frequencyOf(resultForReading(fixture().lookup(u"日"_s), u"か"_s)), 1105);
}

TEST(LookupJL, Frequencyあんまり)
{
    EXPECT_EQ(frequencyOf(resultForSpelling(fixture().lookup(u"あんまり"_s), u"余り"_s)), 284);
}

TEST(LookupJL, Frequency懐かしい)
{
    EXPECT_EQ(frequencyOf(resultForReading(fixture().lookup(u"懐かしい"_s), u"なつかしい"_s)), 1776);
}

TEST(LookupJL, Frequency廃虚)
{
    EXPECT_EQ(frequencyOf(resultForReading(fixture().lookup(u"廃虚"_s), u"はいきょ"_s)), 8560);
}

TEST(LookupJL, Frequency廃墟IsAbsent)
{
    // The list covers 廃虚 read はいきょ, not 廃墟. Attaching 8560 here is the cross-check
    // frequencyFor() exists to prevent.
    const Result result = resultForReading(fixture().lookup(u"廃墟"_s), u"はいきょ"_s);
    EXPECT_EQ(result.primarySpelling, u"廃墟"_s);
    EXPECT_TRUE(result.frequencies.isEmpty());
    EXPECT_EQ(frequencyOf(result), std::numeric_limits<int>::max());
}

TEST(LookupJL, FrequencyＨ)
{
    EXPECT_EQ(frequencyOf(resultForReading(fixture().lookup(u"Ｈ"_s), u"エッチ"_s)), 510);
}

TEST(LookupJL, Frequency咫IsAbsent)
{
    // 咫 is read た, and the list holds three headwords under た, none of them this one.
    const Result result = resultForReading(fixture().lookup(u"咫"_s), u"た"_s);
    EXPECT_EQ(result.primarySpelling, u"咫"_s);
    EXPECT_EQ(frequencyOf(result), std::numeric_limits<int>::max());
}

// ---------------------------------------------------------------------------------------------
// Candidate generation.
// ---------------------------------------------------------------------------------------------

TEST(LookupTextInfo, BuildsEveryPrefixLongestFirst)
{
    const TextInfo info = buildTextInfo(fixture().rules(), u"食べたい人"_s);
    QStringList texts;
    for (const Candidate &candidate : info.candidates)
        texts.append(candidate.text);
    EXPECT_EQ(texts, (QStringList{u"食べたい人"_s, u"食べたい"_s, u"食べた"_s, u"食べ"_s, u"食"_s}));

    // The keys are the normalized forms, and the longest candidate deconjugates to nothing while
    // 食べた reaches 食べる.
    EXPECT_EQ(info.candidates.at(2).key, u"食べた"_s);
    EXPECT_TRUE(info.deconjugatedTexts.contains(u"食べる"_s));
}

TEST(LookupTextInfo, NeverSplitsASurrogatePair)
{
    // U+20B9F, a kanji outside the basic plane, followed by a kana.
    const QString text = QString::fromUcs4(U"\U00020B9Fる");
    ASSERT_EQ(text.size(), 3);

    const TextInfo info = buildTextInfo(fixture().rules(), text);
    QStringList texts;
    for (const Candidate &candidate : info.candidates)
        texts.append(candidate.text);
    // Three code units, two code points, two candidates: the truncation that would leave a lone
    // high surrogate is skipped.
    EXPECT_EQ(texts, (QStringList{text, text.left(2)}));
    for (const QString &candidate : texts)
        EXPECT_FALSE(candidate.back().isHighSurrogate());
}

TEST(LookupTextInfo, SkipsDeconjugationOfALoneFuseji)
{
    const QString fuseji = QString(QChar(jp::kNormalizedFuseji));
    const TextInfo info = buildTextInfo(fixture().rules(), fuseji);
    ASSERT_EQ(info.candidates.size(), 1);
    EXPECT_TRUE(info.candidates.first().forms.empty());
}

TEST(LookupEngine, ClampsTheSpanToMaxSearchLength)
{
    Request request;
    request.sourceText = u"食べたい人"_s;
    request.maxSearchLength = 2;
    const Response response = fixture().engine().lookup(request);
    ASSERT_FALSE(response.results.isEmpty());
    // Only 食べ and 食 can be candidates, so no result can span more than two code units.
    for (const Result &result : response.results)
        EXPECT_LE(result.matchedText.size(), 2);
}

// ---------------------------------------------------------------------------------------------
// Matching.
// ---------------------------------------------------------------------------------------------

TEST(LookupEngine, MatchesADeconjugatedFormEndToEnd)
{
    const Response response = fixture().lookup(u"食べさせられなかった"_s);
    const Result result = resultForSpelling(response, u"食べる"_s);
    ASSERT_EQ(result.primarySpelling, u"食べる"_s);
    EXPECT_EQ(result.matchedText, u"食べさせられなかった"_s);
    // The lemma is the normalized key the deconjugator reached, which folds kana and leaves
    // kanji alone: 食べさせられなかった normalizes to itself and deconjugates to 食べる.
    EXPECT_EQ(result.deconjugatedMatchedText, u"食べる"_s);
    EXPECT_GT(result.minProperStepCount, 0);
    ASSERT_FALSE(result.deconjugationPaths.isEmpty());
    EXPECT_TRUE(deconjugationProcessText(result).startsWith(QChar(u'～')));
    EXPECT_TRUE(deconjugationProcessText(result).contains(u"causative"_s));
    EXPECT_TRUE(deconjugationProcessText(result).contains(u"past"_s));
}

TEST(LookupEngine, PartOfSpeechGatePrunesTheWrongHomograph)
{
    // かえった deconjugates to かえる as a godan verb. 帰る is one; 変える, whose reading record is
    // filed under the same key, is ichidan and must not be attached.
    const Response response = fixture().lookup(u"帰った"_s);
    ASSERT_FALSE(response.results.isEmpty());
    EXPECT_EQ(resultForSpelling(response, u"帰る"_s).primarySpelling, u"帰る"_s);
    EXPECT_TRUE(resultForSpelling(response, u"変える"_s).primarySpelling.isEmpty());

    // The gate itself, called directly on the two records.
    const dict::DictionaryHandle extra = fixture().handle(fixture().extra);
    ASSERT_TRUE(extra.store != nullptr);
    for (const std::shared_ptr<const dict::Record> &record : dict::find(extra, u"かえる"_s)) {
        const bool godan = acceptsDeconjugationTag(extra.type, *record, u"v5r"_s, fixture().manager().wordClassTable());
        const bool ichidan =
            acceptsDeconjugationTag(extra.type, *record, u"v1"_s, fixture().manager().wordClassTable());
        EXPECT_NE(godan, ichidan) << dict::primarySpelling(*record).toStdString();
    }
}

TEST(LookupEngine, FindsAnEntryByItsReadingAlone)
{
    const Response response = fixture().lookup(u"たべる"_s);
    const Result result = resultForSpelling(response, u"食べる"_s);
    ASSERT_EQ(result.primarySpelling, u"食べる"_s);
    // Criterion 3 fires and criterion 2 does not: the match is on the reading, not the headword.
    EXPECT_GE(readingIndexOfMatchedText(result), 0);
    EXPECT_NE(result.primarySpelling, result.matchedText);
    EXPECT_EQ(primarySpellingOrthographyScore(result), std::numeric_limits<int>::max());
}

TEST(LookupEngine, NormalizesKatakanaInput)
{
    const Response response = fixture().lookup(u"タベル"_s);
    const Result result = resultForSpelling(response, u"食べる"_s);
    EXPECT_EQ(result.primarySpelling, u"食べる"_s);
    // The highlight is the raw katakana, not the hiragana key it was found with.
    EXPECT_EQ(result.matchedText, u"タベル"_s);
}

TEST(LookupEngine, ResolvesAChoonpuVariant)
{
    // ラーメン normalizes to らーめん, which no key holds; the elongation variant らあめん does.
    const Response response = fixture().lookup(u"ラーメン"_s);
    const Result result = resultForSpelling(response, u"拉麺"_s);
    EXPECT_EQ(result.primarySpelling, u"拉麺"_s);
    EXPECT_EQ(result.matchedText, u"ラーメン"_s);
}

TEST(LookupEngine, KeepsTheRawTextOfANormalizedMatch)
{
    // 々 expands to the character it repeats, and a small tsu after a small tsu is dropped, so
    // the key of both spans differs from the raw text. The highlight must count the raw one.
    const Response 々 = fixture().lookup(u"日々"_s);
    ASSERT_FALSE(々.results.isEmpty());
    EXPECT_EQ(々.results.first().matchedText, u"日々"_s);
    EXPECT_EQ(々.highlightLength, 2);

    const Response tsu = fixture().lookup(u"やっっぱり"_s);
    ASSERT_FALSE(tsu.results.isEmpty());
    EXPECT_EQ(tsu.results.first().primarySpelling, u"矢っ張り"_s);
    EXPECT_EQ(tsu.results.first().matchedText, u"やっっぱり"_s);
    // Five raw code units against a four code unit key.
    EXPECT_EQ(jp::normalizeText(u"やっっぱり"_s).size(), 4);
    EXPECT_EQ(tsu.highlightLength, 5);
}

TEST(LookupEngine, HighlightsTheLongestMatchFromTheCursor)
{
    Request request;
    request.sourceText = u"その始まるところ"_s;
    request.cursorIndex = 2;
    const Response response = fixture().engine().lookup(request);
    ASSERT_FALSE(response.results.isEmpty());
    EXPECT_EQ(response.highlightStart, 2);
    EXPECT_EQ(response.highlightLength, response.results.first().matchedText.size());
    EXPECT_EQ(response.results.first().matchedText, u"始まる"_s);
}

TEST(LookupEngine, ReturnsNothingForANonJapaneseCursor)
{
    Request request;
    request.sourceText = u"hello 始まる"_s;
    request.cursorIndex = 0;
    const Response response = fixture().engine().lookup(request);
    EXPECT_TRUE(response.results.isEmpty());
    EXPECT_EQ(response.highlightLength, 0);

    // Out of range and an empty text are answered the same way.
    request.cursorIndex = 100;
    EXPECT_TRUE(fixture().engine().lookup(request).results.isEmpty());
    request.sourceText.clear();
    request.cursorIndex = 0;
    EXPECT_TRUE(fixture().engine().lookup(request).results.isEmpty());
}

TEST(LookupEngine, StopsAtAnExpressionBoundary)
{
    Request request;
    request.sourceText = u"日。々"_s;
    const Response response = fixture().engine().lookup(request);
    ASSERT_FALSE(response.results.isEmpty());
    // The sentence terminator ends the span, so 日々 is never a candidate.
    for (const Result &result : response.results)
        EXPECT_FALSE(result.matchedText.contains(QChar(u'々')));
}

TEST(LookupEngine, CapsTheResultCountWithoutReordering)
{
    const Response all = fixture().lookup(u"た"_s, LookupCategory::All, 10);
    ASSERT_GT(all.results.size(), 2);
    const Response capped = fixture().lookup(u"た"_s, LookupCategory::All, 2);
    ASSERT_EQ(capped.results.size(), 2);
    for (qsizetype i = 0; i < capped.results.size(); ++i)
        EXPECT_EQ(capped.results.at(i).primarySpelling, all.results.at(i).primarySpelling);
}

TEST(LookupEngine, AppliesTheCategoryFilter)
{
    const Response words = fixture().lookup(u"田中"_s, LookupCategory::Word);
    for (const Result &result : words.results)
        EXPECT_TRUE(dict::isWordDictionaryType(result.dictionary.type));

    const Response names = fixture().lookup(u"田中"_s, LookupCategory::Name);
    ASSERT_FALSE(names.results.isEmpty());
    for (const Result &result : names.results)
        EXPECT_EQ(result.dictionary.type, DictType::JMnedict);

    const Response kanji = fixture().lookup(u"日"_s, LookupCategory::Kanji);
    ASSERT_FALSE(kanji.results.isEmpty());
    for (const Result &result : kanji.results)
        EXPECT_TRUE(result.kanjiRecord != nullptr);
}

TEST(LookupEngine, CachesAndInvalidates)
{
    Engine &engine = fixture().engine();
    engine.invalidateCache();
    EXPECT_EQ(engine.cachedSpanCount(), 0);

    const QList<Result> first = engine.lookupText(u"始まる"_s, LookupCategory::All, 10);
    EXPECT_EQ(engine.cachedSpanCount(), 1);
    const QList<Result> second = engine.lookupText(u"始まる"_s, LookupCategory::All, 10);
    EXPECT_EQ(engine.cachedSpanCount(), 1);
    ASSERT_EQ(first.size(), second.size());
    EXPECT_EQ(first.first().primarySpelling, second.first().primarySpelling);

    // A different category is a different cache entry.
    (void)engine.lookupText(u"始まる"_s, LookupCategory::Word, 10);
    EXPECT_EQ(engine.cachedSpanCount(), 2);

    engine.invalidateCache();
    EXPECT_EQ(engine.cachedSpanCount(), 0);
}

TEST(LookupEngine, AnswersFromTheSetItWasLastGiven)
{
    Engine &engine = fixture().engine();
    dict::DictionaryManager &manager = fixture().manager();
    engine.invalidateCache();

    ASSERT_TRUE(manager.setEnabled(fixture().frequency, false));
    fixture().refreshEngine();
    const QList<Result> without = engine.lookupText(u"始まる"_s, LookupCategory::All, 10);
    ASSERT_FALSE(without.isEmpty());
    EXPECT_TRUE(without.first().frequencies.isEmpty());
    // With no frequency dictionary the comparator falls back to JMdict's own priority band.
    EXPECT_GT(without.first().priorityRank, 0);
    EXPECT_EQ(frequencyScore(without.first()), without.first().priorityRank);

    ASSERT_TRUE(manager.setEnabled(fixture().frequency, true));
    fixture().refreshEngine();
    const QList<Result> with = engine.lookupText(u"始まる"_s, LookupCategory::All, 10);
    ASSERT_FALSE(with.isEmpty());
    ASSERT_FALSE(with.first().frequencies.isEmpty());
    // The fallback is cleared while a frequency dictionary answers, so the two never compete.
    EXPECT_EQ(with.first().priorityRank, 0);
    EXPECT_EQ(frequencyScore(with.first()), 759);
    engine.invalidateCache();
}

// ---------------------------------------------------------------------------------------------
// Decoration.
// ---------------------------------------------------------------------------------------------

TEST(LookupDecorate, AttachesPitchPositionsPerReading)
{
    const Result result = resultForReading(fixture().lookup(u"Ｈ"_s), u"エッチ"_s);
    ASSERT_EQ(result.readings.size(), 2);
    ASSERT_EQ(result.pitchPositions.size(), 2);
    ASSERT_TRUE(result.pitchPositions.at(0).has_value());
    EXPECT_EQ(*result.pitchPositions.at(0), 1);
    // エイチ is not in the fixture, and an uncovered reading stays empty rather than becoming 0.
    EXPECT_FALSE(result.pitchPositions.at(1).has_value());
}

TEST(LookupDecorate, BuildsTheKanjiCard)
{
    const Response response = fixture().lookup(u"語"_s, LookupCategory::Kanji);
    ASSERT_FALSE(response.results.isEmpty());
    const Result &result = response.results.first();
    ASSERT_TRUE(result.kanjiRecord);
    const dict::KanjidicRecord *record = dict::asKanjidic(*result.kanjiRecord);
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(record->definitions.isEmpty());
    // 語 decomposes into 言 and 吾 in the cjkvi-ids fixture; neither is in the mock KANJIDIC2, so
    // both are shown as the bare character.
    EXPECT_EQ(result.kanjiComponents, (QList<QString>{u"言"_s, u"吾"_s}));
}

// ---------------------------------------------------------------------------------------------
// The comparator, over hand-built results.
// ---------------------------------------------------------------------------------------------

namespace
{

std::shared_ptr<const dict::Record> makeJmdictRecord(const dict::JmdictRecord &record)
{
    auto stored = std::make_shared<dict::Record>();
    stored->type = DictType::JMdict;
    stored->data = record;
    return stored;
}

std::shared_ptr<const dict::Record> makeYomitanRecord(double popularity)
{
    dict::YomitanTermRecord record;
    record.primarySpelling = u"語"_s;
    record.popularityScore = popularity;
    auto stored = std::make_shared<dict::Record>();
    stored->type = DictType::YomitanWord;
    stored->data = record;
    return stored;
}

// A minimal result: a JMdict record carrying the fields the criteria read.
Result makeResult(const QString &matchedText, const QString &primarySpelling, const QList<QString> &readings = {})
{
    dict::JmdictRecord record;
    record.primarySpelling = primarySpelling;
    record.readings = readings;
    record.definitions = {{u"gloss"_s}};

    Result result;
    result.matchedText = matchedText;
    result.primarySpelling = primarySpelling;
    result.readings = readings;
    result.record = makeJmdictRecord(record);
    return result;
}

} // namespace

TEST(LookupRanking, Criterion1LongestMatchedTextFirst)
{
    const Result longer = makeResult(u"食べたい"_s, u"食べたい"_s);
    const Result shorter = makeResult(u"食べ"_s, u"食べ"_s);
    EXPECT_TRUE(lessThan(longer, shorter));
    EXPECT_FALSE(lessThan(shorter, longer));
}

TEST(LookupRanking, Criterion2ExactHeadwordFirst)
{
    const Result exact = makeResult(u"食べる"_s, u"食べる"_s);
    const Result inflected = makeResult(u"食べる"_s, u"喰べる"_s);
    EXPECT_TRUE(lessThan(exact, inflected));
}

TEST(LookupRanking, Criterion3ReadingMatchBeatsNeither)
{
    const Result reading = makeResult(u"たべる"_s, u"食べる"_s, {u"たべる"_s});
    const Result neither = makeResult(u"たべる"_s, u"喰べる"_s, {u"くべる"_s});
    EXPECT_TRUE(lessThan(reading, neither));
}

TEST(LookupRanking, Criterion4FewerDeconjugationStepsFirst)
{
    Result direct = makeResult(u"食べる"_s, u"食べる"_s);
    Result deconjugated = makeResult(u"食べる"_s, u"食べる"_s);
    deconjugated.minProperStepCount = 2;
    EXPECT_TRUE(lessThan(direct, deconjugated));
}

TEST(LookupRanking, Criterion5DictionaryPriority)
{
    dict::DictionaryHandle first;
    first.priority = 1;
    dict::DictionaryHandle second;
    second.priority = 2;

    Result preferred = makeResult(u"食べる"_s, u"食べる"_s);
    preferred.dictionary = first;
    Result other = makeResult(u"食べる"_s, u"食べる"_s);
    other.dictionary = second;
    EXPECT_TRUE(lessThan(preferred, other));
}

TEST(LookupRanking, Criterion6PrimarySpellingOrthography)
{
    dict::JmdictRecord plain;
    plain.primarySpelling = u"余り"_s;
    plain.definitions = {{u"remainder"_s}};
    dict::JmdictRecord outdated = plain;
    outdated.primarySpellingOrthographyInfo = {u"oK"_s};

    Result good = makeResult(u"余り"_s, u"余り"_s);
    good.record = makeJmdictRecord(plain);
    Result rare = makeResult(u"余り"_s, u"余り"_s);
    rare.record = makeJmdictRecord(outdated);

    EXPECT_EQ(primarySpellingOrthographyScore(good), 0);
    EXPECT_EQ(primarySpellingOrthographyScore(rare), 1);
    EXPECT_TRUE(lessThan(good, rare));
}

TEST(LookupRanking, Criterion7ReadingOrthography)
{
    dict::JmdictRecord base;
    base.primarySpelling = u"食べる"_s;
    base.readings = {u"たべる"_s};
    base.definitions = {{u"to eat"_s}};

    dict::JmdictRecord outdated = base;
    outdated.readingsOrthographyInfo = {{u"ok"_s}};
    dict::JmdictRecord kana = base;
    kana.misc.sharedByAllSenses = {u"uk"_s};

    Result plain = makeResult(u"たべる"_s, u"食べる"_s, {u"たべる"_s});
    plain.record = makeJmdictRecord(base);
    Result rare = makeResult(u"たべる"_s, u"食べる"_s, {u"たべる"_s});
    rare.record = makeJmdictRecord(outdated);
    Result usuallyKana = makeResult(u"たべる"_s, u"食べる"_s, {u"たべる"_s});
    usuallyKana.record = makeJmdictRecord(kana);

    EXPECT_EQ(readingOrthographyScore(plain), 1);
    EXPECT_EQ(readingOrthographyScore(rare), 2);
    EXPECT_EQ(readingOrthographyScore(usuallyKana), 0);
    EXPECT_TRUE(lessThan(usuallyKana, plain));
    EXPECT_TRUE(lessThan(plain, rare));
}

TEST(LookupRanking, Criterion8Frequency)
{
    Result common = makeResult(u"食べる"_s, u"食べる"_s);
    common.frequencies = {FrequencyHit{.dictionaryName = u"Freq"_s, .rank = 100, .higherIsBetter = false}};
    Result rare = makeResult(u"食べる"_s, u"食べる"_s);
    rare.frequencies = {FrequencyHit{.dictionaryName = u"Freq"_s, .rank = 5000, .higherIsBetter = false}};
    Result absent = makeResult(u"食べる"_s, u"食べる"_s);

    EXPECT_EQ(frequencyScore(common), 100);
    EXPECT_EQ(frequencyScore(absent), std::numeric_limits<int>::max());
    EXPECT_TRUE(lessThan(common, rare));
    EXPECT_TRUE(lessThan(rare, absent));

    // An occurrence count is mirrored, so the larger value ranks first.
    Result many = makeResult(u"食べる"_s, u"食べる"_s);
    many.frequencies = {FrequencyHit{.dictionaryName = u"Corpus"_s, .rank = 5000, .higherIsBetter = true}};
    Result few = makeResult(u"食べる"_s, u"食べる"_s);
    few.frequencies = {FrequencyHit{.dictionaryName = u"Corpus"_s, .rank = 100, .higherIsBetter = true}};
    EXPECT_TRUE(lessThan(many, few));

    // The JMdict priority band stands in while no frequency dictionary answered.
    Result priority = makeResult(u"食べる"_s, u"食べる"_s);
    priority.priorityRank = 500;
    EXPECT_EQ(frequencyScore(priority), 500);
    EXPECT_TRUE(lessThan(priority, absent));
    // A headword with no priority tag ranks 0, which means absent rather than most common.
    Result unranked = makeResult(u"食べる"_s, u"食べる"_s);
    unranked.priorityRank = 0;
    EXPECT_EQ(frequencyScore(unranked), std::numeric_limits<int>::max());
}

TEST(LookupRanking, Criterion8SecondaryFrequencyBreaksATie)
{
    Result left = makeResult(u"食べる"_s, u"食べる"_s, {u"たべる"_s});
    left.frequencies = {FrequencyHit{.dictionaryName = u"A"_s, .rank = 100, .higherIsBetter = false},
                        FrequencyHit{.dictionaryName = u"B"_s, .rank = 10, .higherIsBetter = false}};
    Result right = makeResult(u"食べる"_s, u"喰べる"_s, {u"くべる"_s});
    right.frequencies = {FrequencyHit{.dictionaryName = u"A"_s, .rank = 100, .higherIsBetter = false},
                         FrequencyHit{.dictionaryName = u"B"_s, .rank = 900, .higherIsBetter = false}};
    EXPECT_TRUE(lessThan(left, right));
}

TEST(LookupRanking, Criterion9YomitanPopularity)
{
    Result popular = makeResult(u"語"_s, u"語"_s);
    popular.record = makeYomitanRecord(50.0);
    Result obscure = makeResult(u"語"_s, u"語"_s);
    obscure.record = makeYomitanRecord(-10.0);
    EXPECT_EQ(popularityScoreOf(popular), 50.0);
    EXPECT_TRUE(lessThan(popular, obscure));
}

TEST(LookupRanking, Criterion10IndexOfTheMatchedReading)
{
    const Result first = makeResult(u"たべる"_s, u"食べる"_s, {u"たべる"_s, u"くべる"_s});
    const Result second = makeResult(u"たべる"_s, u"食べる"_s, {u"くべる"_s, u"たべる"_s});
    EXPECT_EQ(readingIndexOfMatchedText(first), 0);
    EXPECT_EQ(readingIndexOfMatchedText(second), 1);
    EXPECT_TRUE(lessThan(first, second));
}

TEST(LookupRanking, Criterion11EntryId)
{
    dict::JmdictRecord early;
    early.primarySpelling = u"食べる"_s;
    early.entryId = 1000;
    early.definitions = {{u"gloss"_s}};
    dict::JmdictRecord late = early;
    late.entryId = 2000;

    Result first = makeResult(u"食べる"_s, u"食べる"_s);
    first.record = makeJmdictRecord(early);
    Result second = makeResult(u"食べる"_s, u"食べる"_s);
    second.record = makeJmdictRecord(late);
    EXPECT_EQ(entryIdOf(first), 1000);
    EXPECT_TRUE(lessThan(first, second));

    // An entry with no number sorts behind one that has one.
    Result unnumbered = makeResult(u"食べる"_s, u"食べる"_s);
    EXPECT_TRUE(lessThan(second, unnumbered));
}

TEST(LookupRanking, Criterion12PrimarySpellingOrdinal)
{
    const Result a = makeResult(u"あ"_s, u"あ"_s);
    const Result b = makeResult(u"あ"_s, u"い"_s);
    EXPECT_TRUE(lessThan(a, b));
}

TEST(LookupRanking, Criteria13And14DefinitionLengthThenOrdinal)
{
    dict::JmdictRecord shortRecord;
    shortRecord.primarySpelling = u"語"_s;
    shortRecord.definitions = {{u"word"_s}};
    dict::JmdictRecord longRecord = shortRecord;
    longRecord.definitions = {{u"word; language; speech"_s}};

    Result brief = makeResult(u"語"_s, u"語"_s);
    brief.record = makeJmdictRecord(shortRecord);
    Result detailed = makeResult(u"語"_s, u"語"_s);
    detailed.record = makeJmdictRecord(longRecord);
    // The longer definition wins, which is JL's criterion 13.
    EXPECT_TRUE(lessThan(detailed, brief));

    dict::JmdictRecord other = shortRecord;
    other.definitions = {{u"zord"_s}};
    Result alphabetical = makeResult(u"語"_s, u"語"_s);
    alphabetical.record = makeJmdictRecord(other);
    EXPECT_EQ(definitionText(brief), u"word"_s);
    EXPECT_TRUE(lessThan(brief, alphabetical));
    EXPECT_EQ(compareResults(brief, brief), 0);
}

// Engine::lookupUncached() cuts on criteria 1 to 7 before it decorates, so that the frequency
// probe and the pitch probe run over the results that can still enter the answer rather than over
// every result the dictionaries produced. compareUndecorated() is a prefix of the full order, so
// the cut is answer-preserving; this is the assertion of that.
//
// A maxResults larger than the result count takes the cut out of the pipeline, so the two
// lookups compared here differ in whether the cut ran and in nothing else.
TEST(LookupRanking, TheUndecoratedCutAnswersWhatTheFullOrderAnswers)
{
    const QStringList spans = {
        u"た"_s, u"日々"_s, u"日"_s, u"一"_s, u"か"_s, u"語"_s, u"始まる"_s, u"食べさせられなかった"_s};

    qsizetype cutSpans = 0;
    for (const QString &span : spans) {
        const Response whole = fixture().lookup(span, LookupCategory::All, 1000);
        if (whole.results.size() > 3)
            ++cutSpans;
        for (int maxResults = 1; maxResults <= 5; ++maxResults) {
            const Response cut = fixture().lookup(span, LookupCategory::All, maxResults);
            const qsizetype expected = std::min<qsizetype>(maxResults, whole.results.size());
            ASSERT_EQ(cut.results.size(), expected) << qPrintable(span) << " at maxResults " << maxResults;
            for (qsizetype i = 0; i < expected; ++i) {
                const Result &reference = whole.results.at(i);
                const Result &answered = cut.results.at(i);
                const QString where =
                    span + u" at maxResults "_s + QString::number(maxResults) + u" index "_s + QString::number(i);
                EXPECT_EQ(answered.dictionary.id, reference.dictionary.id) << qPrintable(where);
                EXPECT_EQ(answered.primarySpelling, reference.primarySpelling) << qPrintable(where);
                EXPECT_EQ(answered.matchedText, reference.matchedText) << qPrintable(where);
                EXPECT_EQ(answered.readings, reference.readings) << qPrintable(where);
                EXPECT_EQ(answered.deconjugationPaths, reference.deconjugationPaths) << qPrintable(where);
                EXPECT_EQ(definitionText(answered), definitionText(reference)) << qPrintable(where);
                EXPECT_EQ(frequencyScore(answered), frequencyScore(reference)) << qPrintable(where);
                EXPECT_EQ(answered.priorityRank, reference.priorityRank) << qPrintable(where);
                EXPECT_EQ(answered.pitchPositions, reference.pitchPositions) << qPrintable(where);
                EXPECT_EQ(answered.kanjiExamples, reference.kanjiExamples) << qPrintable(where);
                EXPECT_EQ(answered.kanjiComponents, reference.kanjiComponents) << qPrintable(where);
            }
        }
    }

    // A span answered by fewer results than maxResults never reaches the cut, so the suite fails
    // if the fixture stops producing the lists that do reach it. The mock set answers た, 日々 and
    // 日 with four or five results each, which the loop cuts at every maxResults from 1 to 4.
    EXPECT_GE(cutSpans, 3);
}

// ---------------------------------------------------------------------------------------------
// The popup adapter.
// ---------------------------------------------------------------------------------------------

TEST(LookupAdapter, MapsAResponseOntoThePopupModel)
{
    const Response response = fixture().lookup(u"始まる"_s);
    const popup::PopupModel model = toPopupModel(response);
    ASSERT_FALSE(model.entries.isEmpty());
    const popup::Entry &entry = model.entries.first();

    EXPECT_EQ(model.matchedText, u"始まる"_s);
    EXPECT_EQ(entry.headword, u"始まる"_s);
    EXPECT_EQ(entry.readings, QStringList{u"はじまる"_s});
    EXPECT_EQ(entry.dictionaryName, u"JMdict"_s);
    ASSERT_EQ(entry.pitchPositions.size(), entry.readings.size());
    ASSERT_TRUE(entry.pitchPositions.first().has_value());
    EXPECT_EQ(*entry.pitchPositions.first(), 0);

    // One frequency dictionary: the rank alone, and no "Name: rank" tail.
    ASSERT_TRUE(entry.frequencyRank.has_value());
    EXPECT_EQ(*entry.frequencyRank, 759);
    EXPECT_TRUE(entry.frequencyText.isEmpty());

    ASSERT_EQ(entry.senses.size(), 3);
    EXPECT_EQ(entry.senses.first().glosses, (QStringList{u"to begin"_s, u"to start"_s, u"to commence"_s}));
    EXPECT_EQ(entry.senses.first().pos, (QStringList{u"v5r"_s, u"vi"_s}));
    EXPECT_TRUE(entry.richTextGlossary.isEmpty());
}

TEST(LookupAdapter, RendersTheDeconjugationPathTheWayTheRendererJoinsIt)
{
    const Response response = fixture().lookup(u"食べさせられなかった"_s);
    const popup::PopupModel model = toPopupModel(response);
    ASSERT_FALSE(model.entries.isEmpty());
    const popup::Entry &entry = model.entries.first();
    ASSERT_FALSE(entry.deconjugationPaths.isEmpty());
    // The renderer joins the paths with "; " inside one pair of parentheses, so only the first
    // carries JL's U+FF5E marker.
    EXPECT_TRUE(entry.deconjugationPaths.first().startsWith(QChar(u'～')));
    for (qsizetype i = 1; i < entry.deconjugationPaths.size(); ++i)
        EXPECT_FALSE(entry.deconjugationPaths.at(i).startsWith(QChar(u'～')));
    // The card drops a repeated path, so its join is the distinct prefix of the JL-shaped text
    // rather than that text itself.
    for (qsizetype i = 0; i < entry.deconjugationPaths.size(); ++i) {
        for (qsizetype j = i + 1; j < entry.deconjugationPaths.size(); ++j)
            EXPECT_NE(entry.deconjugationPaths.at(i), entry.deconjugationPaths.at(j));
    }
    QStringList expected;
    for (const QString &path : response.results.first().deconjugationPaths) {
        if (!expected.contains(path))
            expected.append(path);
    }
    expected.first().prepend(QChar(u'～'));
    EXPECT_EQ(entry.deconjugationPaths, expected);
}

// 食べさせられなかった reaches "passive/potential/honorific→negative→past" through two distinct
// ProcessNode chains, so the lookup answers two identical strings. JL renders both
// (LookupResultUtils.DeconjugationProcessesToText) and
// lookup::deconjugationProcessText() matches JL; the popup card shows each distinct path once.
TEST(LookupAdapter, ShowsOneCopyOfARepeatedDeconjugationPath)
{
    Result result = makeResult(u"食べさせられなかった"_s, u"食べる"_s, {u"たべる"_s});
    result.deconjugationPaths = {u"passive/potential/honorific→negative→past"_s,
                                 u"passive/potential/honorific→negative→past"_s,
                                 u"causative→negative→past"_s};
    const popup::Entry entry = toPopupEntry(result);
    EXPECT_EQ(entry.deconjugationPaths,
              (QStringList{u"～passive/potential/honorific→negative→past"_s, u"causative→negative→past"_s}));
    // The joined text the lookup reports still carries both copies, which is what JL prints.
    EXPECT_EQ(deconjugationProcessText(result),
              u"～passive/potential/honorific→negative→past; passive/potential/honorific→negative→past; "
              "causative→negative→past"_s);
}

TEST(LookupAdapter, RendersSeveralFrequenciesAsNameAndRank)
{
    Result result = makeResult(u"食べる"_s, u"食べる"_s, {u"たべる"_s});
    result.frequencies = {FrequencyHit{.dictionaryName = u"VN"_s, .rank = 759, .higherIsBetter = false},
                          FrequencyHit{.dictionaryName = u"Narou"_s, .rank = 1200, .higherIsBetter = false},
                          FrequencyHit{.dictionaryName = u"Novel"_s, .rank = 88, .higherIsBetter = false}};
    const popup::Entry entry = toPopupEntry(result);
    ASSERT_TRUE(entry.frequencyRank.has_value());
    EXPECT_EQ(*entry.frequencyRank, 759);
    EXPECT_EQ(entry.frequencyText, u"Narou: 1200, Novel: 88"_s);

    // The clipboard rendering is JL's FrequenciesToText, which collapses a single dictionary to
    // the bare rank.
    EXPECT_EQ(frequenciesToText(std::span<const FrequencyHit>(result.frequencies.cbegin(), 1)), u"#759"_s);
    EXPECT_EQ(frequenciesToText(std::span<const FrequencyHit>(result.frequencies.cbegin(), result.frequencies.size())),
              u"VN: 759, Narou: 1200, Novel: 88"_s);
}

// The popup and the lookup window list two counts of one answer, so the first results of a
// longer answer have to be the answer of a request for that count, in the same order.
TEST(LookupAdapter, FirstResultsOfALongerAnswerAreTheShorterAnswer)
{
    Response longer;
    longer.highlightStart = 2;
    longer.highlightLength = 3;
    for (const QString &spelling : {u"始まる"_s, u"始め"_s, u"始"_s}) {
        Result result;
        result.primarySpelling = spelling;
        longer.results.append(result);
    }

    const Response cut = firstResults(longer, 2);
    ASSERT_EQ(cut.results.size(), 2);
    EXPECT_EQ(cut.results.at(0).primarySpelling, u"始まる"_s);
    EXPECT_EQ(cut.results.at(1).primarySpelling, u"始め"_s);
    EXPECT_EQ(cut.highlightStart, longer.highlightStart);
    EXPECT_EQ(cut.highlightLength, longer.highlightLength);

    EXPECT_EQ(firstResults(longer, 0).results.size(), longer.results.size());
    EXPECT_EQ(firstResults(longer, 1000).results.size(), longer.results.size());
}

TEST(LookupAdapter, BuildsTheKanjiCardFromTheKanjiResult)
{
    const Response response = fixture().lookup(u"日"_s);
    const popup::PopupModel model = toPopupModel(response);
    ASSERT_TRUE(model.kanji.has_value());
    EXPECT_EQ(model.kanji->character, u"日"_s);
    EXPECT_FALSE(model.kanji->onReadings.isEmpty());
    EXPECT_FALSE(model.kanji->meanings.isEmpty());
    EXPECT_EQ(model.kanji->strokeCount, 4);
    ASSERT_TRUE(model.kanji->grade.has_value());
    EXPECT_EQ(*model.kanji->grade, 1);
    // The kanji result becomes the card and not also an entry, so the two lists together
    // account for every result.
    qsizetype kanjiResults = 0;
    for (const Result &result : response.results)
        kanjiResults += result.kanjiRecord ? 1 : 0;
    ASSERT_EQ(kanjiResults, 1);
    EXPECT_EQ(model.entries.size(), response.results.size() - 1);
}

TEST(LookupAdapter, ExpandsAnEntityCodeToItsDescription)
{
    const dict::DictionaryHandle jmdict = fixture().handle(fixture().jmdict);
    ASSERT_TRUE(jmdict.store != nullptr);
    EXPECT_EQ(entityDescription(jmdict, u"v5r"_s), u"Godan verb with 'ru' ending"_s);
    // An unknown code is returned unchanged, which is what a Yomitan tag does.
    EXPECT_EQ(entityDescription(jmdict, u"not-an-entity"_s), u"not-an-entity"_s);
}

TEST(LookupEngine, AnswersFromSeveralThreadsAtOnce)
{
    Engine &engine = fixture().engine();
    engine.invalidateCache();

    const QStringList spans{u"始まる"_s, u"食べさせられなかった"_s, u"日"_s, u"た"_s, u"ラーメン"_s, u"帰った"_s};
    std::vector<std::thread> threads;
    std::atomic_int failures{0};
    threads.reserve(4);
    for (int thread = 0; thread < 4; ++thread) {
        threads.emplace_back([&engine, &spans, &failures] {
            for (int round = 0; round < 50; ++round) {
                for (const QString &span : spans) {
                    const QList<Result> results = engine.lookupText(span, LookupCategory::All, 10);
                    if (results.isEmpty())
                        ++failures;
                }
            }
        });
    }
    for (std::thread &thread : threads)
        thread.join();

    EXPECT_EQ(failures.load(), 0);
    EXPECT_EQ(engine.cachedSpanCount(), spans.size());
    engine.invalidateCache();
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
