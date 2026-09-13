// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The recognition-variant pass: lookup/ocrvariants.h on its own, and lookup::Engine with
// setVariantOptions() over tests/data/lookup/mock_variants.xml.
#include "deconj/deconjugator.h"
#include "dict/dictionary.h"
#include "dict/dictionarymanager.h"
#include "dict/importers/jmdictimporter.h"
#include "dict/store.h"
#include "jp/japanese.h"
#include "lookup/engine.h"
#include "lookup/ocrvariants.h"
#include "lookup/textinfo.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTemporaryDir>
#include <QUuid>

#include <algorithm>
#include <atomic>
#include <gtest/gtest.h>
#include <memory>

using namespace Qt::Literals::StringLiterals;
using namespace maru;
using namespace maru::lookup;

namespace
{

class Fixture
{
public:
    Fixture()
    {
        EXPECT_TRUE(m_directory.isValid());
        m_manager = std::make_unique<dict::DictionaryManager>(m_directory.path(), nullptr);

        dict::Dictionary entry;
        entry.type = dict::DictType::JMdict;
        entry.name = QStringLiteral("JMdict Variants");
        entry.sourcePath = QStringLiteral(MARUPOP_LOOKUP_TEST_DATA_DIR) + u"/mock_variants.xml"_s;
        entry.priority = 1;
        dict::Dictionary *stored = m_manager->add(entry);
        EXPECT_NE(stored, nullptr);
        if (stored != nullptr) {
            dict::JmdictImporter importer(true);
            dict::StoreWriter writer;
            EXPECT_TRUE(writer.begin(dict::databasePathFor(m_directory.path(), stored->id), entry.type));
            std::atomic_bool cancel{false};
            const dict::ImportResult result =
                importer.import(entry.sourcePath, writer, [](int, const QString &) {}, cancel);
            EXPECT_TRUE(result.ok) << result.errorString.toStdString();
            stored->recordCount = result.recordCount;
            stored->keyCount = result.keyCount;
            stored->maxKeyLength = result.maxKeyLength;
            stored->importedAt = QDateTime::currentDateTimeUtc();
            EXPECT_TRUE(importer.wordClassTable().save(dict::wordClassTablePathFor(m_directory.path(), stored->id)));
        }

        m_manager->openAll();
        EXPECT_TRUE(m_manager->loadWordClassTable());
        std::optional<deconj::RuleSet> rules = deconj::RuleSet::loadEmbedded();
        EXPECT_TRUE(rules.has_value());
        m_rules = std::move(rules);
        m_engine = std::make_unique<Engine>(*m_manager, *m_rules);
    }

    [[nodiscard]] Engine &engine()
    {
        return *m_engine;
    }

    [[nodiscard]] const deconj::RuleSet &rules() const
    {
        return *m_rules;
    }

    [[nodiscard]] Response
    lookup(const QString &text, const VariantOptions &options, const QList<float> &confidences = {})
    {
        m_engine->setVariantOptions(options);
        Request request;
        request.sourceText = text;
        request.maxResults = 10;
        request.confidences = confidences;
        return m_engine->lookup(request);
    }

    [[nodiscard]] Response lookup(const QString &text, bool variants, const QList<float> &confidences = {})
    {
        VariantOptions options;
        options.enabled = variants;
        options.withoutConfidences = true;
        return lookup(text, options, confidences);
    }

private:
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

// Every option at its most permissive, so that every candidate and every position gets variants.
VariantOptions unlimited()
{
    VariantOptions options;
    options.enabled = true;
    options.withoutConfidences = true;
    options.maxKeyLength = 0;
    options.maxKeys = 0;
    options.shortAndHiraganaMatches = true;
    return options;
}

QStringList describe(const std::vector<deconj::Form> &forms)
{
    QStringList out;
    for (const deconj::Form &form : forms)
        out.append(form.text + u'|' + form.lastTag.toString() + u'|' + deconj::formattedProcess(form.process));
    return out;
}

} // namespace

TEST(OcrVariants, substitutesVoicedAndSemiVoicedKanaInTheHiraganaKeySpace)
{
    EXPECT_EQ(substitutesFor(QChar(u'は')), u"ばぱ"_s);
    EXPECT_EQ(substitutesFor(QChar(u'ぱ')), u"はば"_s);
    EXPECT_EQ(substitutesFor(QChar(u'て')), u"で"_s);
    EXPECT_TRUE(substitutesFor(QChar(u'ん')).isEmpty());
    // A key is hiragana, so katakana reaches the table through jp::normalizeText().
    EXPECT_EQ(jp::normalizeText(u"バ"_s), u"ば"_s);
}

TEST(OcrVariants, substitutesSmallAndFullSizeKana)
{
    EXPECT_EQ(substitutesFor(QChar(u'あ')), u"ぁ"_s);
    EXPECT_EQ(substitutesFor(QChar(u'っ')), u"つ"_s);
    EXPECT_TRUE(substitutesFor(QChar(u'つ')).contains(QChar(u'っ')));
    EXPECT_TRUE(substitutesFor(QChar(u'つ')).contains(QChar(u'づ')));
    // う belongs to two groups.
    EXPECT_EQ(substitutesFor(QChar(u'う')), u"ゔぅ"_s);
    // ヶ read for ケ, the most frequent small-kana misread of katakana in the test shards.
    const QString smallKe = jp::normalizeText(u"ヶ"_s);
    ASSERT_EQ(smallKe.size(), 1);
    EXPECT_TRUE(substitutesFor(smallKe.front()).contains(QChar(u'け')));
}

TEST(OcrVariants, substitutesSimilarKanjiInBothDirections)
{
    EXPECT_TRUE(substitutesFor(QChar(u'牛')).contains(QChar(u'生')));
    EXPECT_TRUE(substitutesFor(QChar(u'生')).contains(QChar(u'牛')));
    EXPECT_TRUE(substitutesFor(QChar(u'某')).contains(QChar(u'基')));
    EXPECT_TRUE(substitutesFor(QChar(u'大')).contains(QChar(u'太')));
    EXPECT_FALSE(substitutesFor(QChar(u'大')).contains(QChar(u'大')));
}

TEST(OcrVariants, substitutesAcrossScriptsByTheSourceCharacter)
{
    EXPECT_EQ(lookalikesFor(QChar(u'カ')), u"力"_s);
    EXPECT_EQ(lookalikesFor(QChar(u'力')), u"カ"_s);
    EXPECT_EQ(lookalikesFor(QChar(u'ト')), u"上卜"_s);
    // レ keys as れ, so the lookalike し is reached through the source character alone: れ read
    // from hiragana is not a misread of し.
    EXPECT_TRUE(keySubstitutes(QChar(u'れ'), QChar(u'レ')).contains(QChar(u'し')));
    EXPECT_FALSE(keySubstitutes(QChar(u'れ'), QChar(u'れ')).contains(QChar(u'し')));
    EXPECT_TRUE(keySubstitutes(QChar(u'し'), QChar(u'し')).contains(QChar(u'れ')));
    EXPECT_TRUE(keySubstitutes(QChar(u'ー'), QChar(u'ー')).contains(QChar(u'一')));
    EXPECT_TRUE(keySubstitutes(QChar(u'一'), QChar(u'一')).contains(QChar(u'ー')));
    // Without an aligned source character only the key-space tables apply.
    EXPECT_FALSE(keySubstitutes(QChar(u'れ'), QChar()).contains(QChar(u'し')));
}

TEST(OcrVariants, reportsThePrefixNoDeconjugationRuleReads)
{
    const deconj::RuleSet &rules = fixture().rules();
    EXPECT_EQ(rules.longestConEnd(), 8);
    qsizetype untouched = -1;
    (void)deconj::deconjugate(rules, u"ありませんでした"_s, &untouched);
    EXPECT_EQ(untouched, 0);
    (void)deconj::deconjugate(rules, u"きほんてきなかんがえ"_s, &untouched);
    EXPECT_GT(untouched, 0);
    EXPECT_LE(untouched, 10 - 8);
}

TEST(OcrVariants, derivesTheFormsDeconjugationWouldProduce)
{
    // Every variant of every candidate, whether its forms were derived from the candidate's or
    // deconjugated, has to hold what deconjugating its key produces, in the same order.
    // Long stems before short endings are where the derived path runs; long endings are where
    // the deconjugated one does.
    const QStringList spans = {
        u"食べさせられなかった"_s,
        u"いらっしゃいませんでした"_s,
        u"読んでいませんでした"_s,
        u"高くなかったら"_s,
        u"話しかけられている"_s,
        u"勉強しなければならない"_s,
        u"ばーてぃーでした"_s,
        u"来させられた"_s,
        u"基本的な考え方を話した"_s,
        u"大学生だった"_s,
        u"ぶらんでぃんぐする"_s,
    };
    const deconj::RuleSet &rules = fixture().rules();
    qsizetype derived = 0;
    for (const QString &span : spans) {
        const TextInfo exact = buildTextInfo(rules, span);
        const TextInfo variants = buildVariantTextInfo(rules, exact, 0, unlimited());
        ASSERT_FALSE(variants.candidates.isEmpty()) << span.toStdString();
        for (const Candidate &candidate : variants.candidates) {
            EXPECT_EQ(describe(candidate.forms), describe(deconj::deconjugate(rules, candidate.key)))
                << candidate.key.toStdString();
            const auto source = std::ranges::find_if(exact.candidates, [&candidate](const Candidate &original) {
                return original.text == candidate.text;
            });
            ASSERT_NE(source, exact.candidates.end());
            for (qsizetype i = 0; i < candidate.key.size(); ++i) {
                if (candidate.key.at(i) != source->key.at(i) && i < source->untouchedPrefix)
                    ++derived;
            }
        }
    }
    // The comparison has to have covered the derived path, not only the deconjugated one.
    EXPECT_GT(derived, 20);
}

TEST(OcrVariants, resolvesTheElongationRunsOfAVariant)
{
    const TextInfo exact = buildTextInfo(fixture().rules(), u"バーティー");
    const TextInfo variants = buildVariantTextInfo(fixture().rules(), exact, 0, unlimited());
    const auto party = std::ranges::find_if(variants.candidates, [](const Candidate &candidate) {
        return candidate.key == u"ぱーてぃー"_s;
    });
    ASSERT_NE(party, variants.candidates.end());
    // The runs of the variant key itself, resolved the way buildTextInfo() resolves an exact key.
    EXPECT_FALSE(party->longVowelVariants.isEmpty());
    EXPECT_EQ(party->longVowelVariants, jp::normalizeLongVowelMark(party->key));
    EXPECT_EQ(party->longVowelVariantForms.size(), static_cast<std::size_t>(party->longVowelVariants.size()));
}

TEST(OcrVariants, buildsCandidatesLongerThanTheExactMatchOnly)
{
    const TextInfo exact = buildTextInfo(fixture().rules(), u"ばーてぃー");
    const TextInfo variants = buildVariantTextInfo(fixture().rules(), exact, 2, unlimited());
    ASSERT_FALSE(variants.candidates.isEmpty());
    for (const Candidate &candidate : variants.candidates)
        EXPECT_GT(candidate.text.size(), 2);
    // Longest candidate first, as queryWordDictionary() needs.
    EXPECT_EQ(variants.candidates.first().text, u"ばーてぃー"_s);
    EXPECT_EQ(variants.candidates.last().text, u"ばーて"_s);
}

TEST(OcrVariants, capsTheVariantKeysOfOneLookupWhereASettingAsksForIt)
{
    // Twelve kanji with similar-kanji neighbours each produce far more than 400 keys.
    const TextInfo exact = buildTextInfo(fixture().rules(), u"末未牛午半羊美平来末未牛");
    VariantOptions options;
    const TextInfo capped = buildVariantTextInfo(fixture().rules(), exact, 0, options);
    EXPECT_LE(capped.candidates.size(), options.maxKeys);
    ASSERT_FALSE(capped.candidates.isEmpty());
    // The budget goes to the shortest candidates, of which a pair of kanji is the shortest
    // admitsVariants() passes.
    EXPECT_EQ(capped.candidates.last().text.size(), 2);

    options.maxKeys = 0;
    const TextInfo whole = buildVariantTextInfo(fixture().rules(), exact, 0, options);
    EXPECT_GT(whole.candidates.size(), capped.candidates.size());
    EXPECT_EQ(whole.candidates.first().text.size(), 12);
}

TEST(OcrVariants, limitsTheKeyLengthWhereASettingAsksForIt)
{
    const TextInfo exact = buildTextInfo(fixture().rules(), u"バーティーバーティーバーティー");
    VariantOptions options;
    options.maxKeys = 0;
    options.maxKeyLength = 12;
    for (const Candidate &candidate : buildVariantTextInfo(fixture().rules(), exact, 0, options).candidates)
        EXPECT_LE(candidate.key.size(), 12);
    options.maxKeyLength = 0;
    EXPECT_EQ(buildVariantTextInfo(fixture().rules(), exact, 0, options).candidates.first().text.size(), 15);
}

TEST(OcrVariantLookup, answersTheExactReadAloneWhileDisabled)
{
    const Response response = fixture().lookup(u"バーティー"_s, false);
    ASSERT_FALSE(response.results.isEmpty());
    EXPECT_EQ(response.results.first().primarySpelling, u"バー"_s);
    EXPECT_EQ(response.highlightLength, 2);
}

TEST(OcrVariantLookup, prefersALongerVariantOverAShorterExactMatch)
{
    const Response response = fixture().lookup(u"バーティー"_s, true);
    ASSERT_FALSE(response.results.isEmpty());
    const Result &first = response.results.first();
    EXPECT_EQ(first.primarySpelling, u"パーティー"_s);
    EXPECT_EQ(first.matchedText, u"バーティー"_s);
    EXPECT_TRUE(first.matchedByVariant);
    EXPECT_EQ(response.highlightLength, 5);
    // The exact match stays in the list, behind the variant.
    const bool hasBar = std::ranges::any_of(response.results, [](const Result &result) {
        return result.primarySpelling == u"バー"_s && !result.matchedByVariant;
    });
    EXPECT_TRUE(hasBar);
}

TEST(OcrVariantLookup, findsAWordThroughASimilarKanji)
{
    const Response response = fixture().lookup(u"某本を"_s, true);
    ASSERT_FALSE(response.results.isEmpty());
    EXPECT_EQ(response.results.first().primarySpelling, u"基本"_s);
    EXPECT_EQ(response.results.first().matchedText, u"某本"_s);
}

TEST(OcrVariantLookup, deconjugatesAVariant)
{
    const Response response = fixture().lookup(u"読んて"_s, true);
    ASSERT_FALSE(response.results.isEmpty());
    const Result &first = response.results.first();
    EXPECT_EQ(first.primarySpelling, u"読む"_s);
    EXPECT_EQ(first.matchedText, u"読んて"_s);
    EXPECT_EQ(first.deconjugatedMatchedText, u"読む"_s);
    EXPECT_FALSE(first.deconjugationPaths.isEmpty());
}

TEST(OcrVariantLookup, keepsAnExactMatchOfEqualLengthAsTheOnlyAnswer)
{
    const Response response = fixture().lookup(u"はは"_s, unlimited());
    ASSERT_FALSE(response.results.isEmpty());
    for (const Result &result : response.results) {
        EXPECT_FALSE(result.matchedByVariant) << result.primarySpelling.toStdString();
        EXPECT_NE(result.primarySpelling, u"婆"_s);
    }
}

TEST(OcrVariantLookup, keysTheCacheOnTheOptions)
{
    // No invalidateCache() between the calls: the options are part of the cache key.
    EXPECT_EQ(fixture().lookup(u"バーティー"_s, true).results.first().primarySpelling, u"パーティー"_s);
    EXPECT_EQ(fixture().lookup(u"バーティー"_s, false).results.first().primarySpelling, u"バー"_s);
    EXPECT_EQ(fixture().lookup(u"バーティー"_s, true).results.first().primarySpelling, u"パーティー"_s);
    VariantOptions tight;
    tight.enabled = true;
    tight.withoutConfidences = true;
    tight.maxKeyLength = 3;
    EXPECT_EQ(fixture().lookup(u"バーティー"_s, tight).results.first().primarySpelling, u"バー"_s);
}

TEST(OcrVariants, substitutesOnlyTheCharactersTheMaskPermits)
{
    const TextInfo exact = buildTextInfo(fixture().rules(), u"ばーてぃー");
    const QByteArray mask("\x00\x01\x01\x01\x01", 5);
    const TextInfo variants = buildVariantTextInfo(fixture().rules(), exact, 0, unlimited(), mask);
    // ば is masked out, so every variant keeps it.
    for (const Candidate &candidate : variants.candidates)
        EXPECT_EQ(candidate.key.at(0), QChar(u'ば')) << candidate.key.toStdString();
}

TEST(OcrVariants, acceptsKatakanaOrACommonWord)
{
    const VariantOptions options;
    Result katakana;
    katakana.matchedText = u"バーティー"_s;
    katakana.primarySpelling = u"パーティー"_s;
    EXPECT_TRUE(acceptsVariantResult(katakana, {}, options));
    // A katakana read that a lookalike across scripts turned into a kanji word is not katakana
    // evidence.
    Result crossed;
    crossed.matchedText = u"タベース"_s;
    crossed.primarySpelling = u"食べる"_s;
    EXPECT_FALSE(acceptsVariantResult(crossed, {}, options));

    Result shortKana;
    shortKana.matchedText = u"けは"_s;
    shortKana.priorityRank = 12000;
    EXPECT_FALSE(acceptsVariantResult(shortKana, {}, options));

    Result kanjiPair;
    kanjiPair.matchedText = u"某本"_s;
    kanjiPair.priorityRank = 12000;
    EXPECT_TRUE(acceptsVariantResult(kanjiPair, {}, options));
    kanjiPair.priorityRank = 0;
    EXPECT_FALSE(acceptsVariantResult(kanjiPair, {}, options));

    // A rank-ordered frequency dictionary is evidence where JMdict's tags are absent, which is
    // every Yomitan term bank; an occurrence count is not.
    const QList<FrequencyHit> common{FrequencyHit{.dictionaryName = u"JPDB"_s, .rank = 900, .higherIsBetter = false}};
    const QList<FrequencyHit> rare{FrequencyHit{.dictionaryName = u"JPDB"_s, .rank = 90000, .higherIsBetter = false}};
    const QList<FrequencyHit> counted{FrequencyHit{.dictionaryName = u"BCCWJ"_s, .rank = 5, .higherIsBetter = true}};
    EXPECT_TRUE(acceptsVariantResult(kanjiPair, common, options));
    EXPECT_FALSE(acceptsVariantResult(kanjiPair, rare, options));
    EXPECT_FALSE(acceptsVariantResult(kanjiPair, counted, options));

    Result tagged;
    tagged.matchedText = u"読んて"_s;
    tagged.priorityRank = 24000;
    EXPECT_FALSE(acceptsVariantResult(tagged, {}, options));
}

TEST(OcrVariants, acceptsEveryMatchWhereTheSettingAsks)
{
    Result rare;
    rare.matchedText = u"某本"_s;
    VariantOptions options;
    // A set of Yomitan term banks with no frequency dictionary marks no word common, so only a
    // katakana match passes the evidence rule there.
    EXPECT_FALSE(acceptsVariantResult(rare, {}, options));
    options.acceptance = VariantAcceptance::AnyWord;
    EXPECT_TRUE(acceptsVariantResult(rare, {}, options));
}

TEST(OcrVariants, admitsShortAndHiraganaMatchesWhereTheSettingAsks)
{
    VariantOptions options;
    EXPECT_FALSE(admitsVariants(u"てかみ", options));
    EXPECT_TRUE(admitsVariants(u"バーティー", options));
    EXPECT_FALSE(admitsVariants(u"は本", options));
    options.shortAndHiraganaMatches = true;
    EXPECT_TRUE(admitsVariants(u"てかみ", options));
    EXPECT_TRUE(admitsVariants(u"は本", options));
    EXPECT_FALSE(admitsVariants(u"本", options));
}

TEST(OcrVariantLookup, declinesAHiraganaOnlyVariant)
{
    const Response response = fixture().lookup(u"てかみ"_s, true);
    for (const Result &result : response.results)
        EXPECT_NE(result.primarySpelling, u"手紙"_s);
}

TEST(OcrVariantLookup, findsAHiraganaOnlyVariantWhereTheSettingsAsk)
{
    // 手紙 carries no priority tag in the fixture, so it needs AnyWord as well.
    VariantOptions options;
    options.enabled = true;
    options.withoutConfidences = true;
    options.shortAndHiraganaMatches = true;
    options.acceptance = VariantAcceptance::AnyWord;
    const Response response = fixture().lookup(u"てかみ"_s, options);
    ASSERT_FALSE(response.results.isEmpty());
    EXPECT_EQ(response.results.first().primarySpelling, u"手紙"_s);
}

TEST(OcrVariantLookup, substitutesOnlyLowConfidenceCharactersWhereConfidencesArePresent)
{
    // バ read at 0.95 is trusted, so パーティー is not tried.
    const QList<float> confident{0.95F, 0.99F, 0.99F, 0.99F, 0.99F};
    EXPECT_EQ(fixture().lookup(u"バーティー"_s, true, confident).results.first().primarySpelling, u"バー"_s);
    // バ read at 0.40 is not.
    const QList<float> doubtful{0.40F, 0.99F, 0.99F, 0.99F, 0.99F};
    EXPECT_EQ(fixture().lookup(u"バーティー"_s, true, doubtful).results.first().primarySpelling, u"パーティー"_s);
    // A list that does not cover the text leaves every character substitutable.
    EXPECT_EQ(fixture().lookup(u"バーティー"_s, true, {0.99F}).results.first().primarySpelling, u"パーティー"_s);
    // A gate of 1 trusts no confidence.
    VariantOptions everyCharacter;
    everyCharacter.enabled = true;
    everyCharacter.confidenceGate = 1.0F;
    EXPECT_EQ(fixture().lookup(u"バーティー"_s, everyCharacter, confident).results.first().primarySpelling,
              u"パーティー"_s);
}

TEST(OcrVariantLookup, skipsTextWithoutConfidencesUnlessTheSettingAsks)
{
    VariantOptions options;
    options.enabled = true;
    // No confidences, which is a Chrome Screen AI read.
    EXPECT_EQ(fixture().lookup(u"バーティー"_s, options).results.first().primarySpelling, u"バー"_s);
    const QList<float> doubtful{0.40F, 0.99F, 0.99F, 0.99F, 0.99F};
    EXPECT_EQ(fixture().lookup(u"バーティー"_s, options, doubtful).results.first().primarySpelling, u"パーティー"_s);
    options.withoutConfidences = true;
    EXPECT_EQ(fixture().lookup(u"バーティー"_s, options).results.first().primarySpelling, u"パーティー"_s);
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
