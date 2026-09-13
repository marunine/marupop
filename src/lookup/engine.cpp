// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "engine.h"

#include "core/logging.h"
#include "dict/dictionarymanager.h"
#include "dict/lookupsupport.h"
#include "dict/records.h"
#include "jp/japanese.h"
#include "lookup/decorate.h"
#include "lookup/ocrvariants.h"
#include "lookup/query.h"
#include "lookup/ranking.h"
#include "lookup/textinfo.h"

#include <QHash>
#include <QMutexLocker>
#include <QStringList>

#include <algorithm>

using namespace Qt::Literals::StringLiterals;

namespace maru::lookup
{

namespace
{

// The one or two code units at the cursor, which is what JL tests for Japanese content before
// starting a lookup at all.
[[nodiscard]] QStringView characterAt(QStringView text, qsizetype position)
{
    const qsizetype length = text.at(position).isHighSurrogate() && position + 1 < text.size() ? 2 : 1;
    return text.sliced(position, length);
}

[[nodiscard]] QString exampleText(const dict::KanjiExample &example)
{
    if (example.reading.isEmpty())
        return example.gloss.isEmpty() ? example.spelling : example.spelling % u" "_s % example.gloss;
    const QString head = example.spelling % u" ["_s % example.reading % u"]"_s;
    return example.gloss.isEmpty() ? head : head % u" "_s % example.gloss;
}

[[nodiscard]] std::size_t categoryIndex(LookupCategory category)
{
    return static_cast<std::size_t>(category);
}

} // namespace

std::size_t Engine::CacheKeyHash::operator()(const CacheKey &key) const noexcept
{
    return qHashMulti(0,
                      key.dictionaries.get(),
                      key.span,
                      static_cast<int>(key.category),
                      key.maxResults,
                      key.variants.enabled,
                      key.variants.confidenceGate,
                      key.variants.withoutConfidences,
                      key.variants.maxKeyLength,
                      key.variants.maxKeys,
                      static_cast<int>(key.variants.acceptance),
                      key.variants.shortAndHiraganaMatches,
                      key.variants.nameDictionaries,
                      key.variants.rankFirst,
                      key.substitutable);
}

Engine::Engine(const deconj::RuleSet &rules)
    : m_rules(rules)
    , m_dictionaries(std::make_shared<const std::vector<dict::DictionaryHandle>>())
    , m_wordClasses(std::make_shared<const dict::WordClassTable>())
{}

Engine::Engine(dict::DictionaryManager &dictionaries, const deconj::RuleSet &rules)
    : Engine(rules)
{
    setDictionaries(dictionaries.snapshot(), dictionaries.wordClasses());
}

Engine::~Engine() = default;

void Engine::setDictionaries(dict::DictionarySnapshot dictionaries,
                             std::shared_ptr<const dict::WordClassTable> wordClasses)
{
    QMutexLocker locker(&m_mutex);
    m_dictionaries =
        dictionaries ? std::move(dictionaries) : std::make_shared<const std::vector<dict::DictionaryHandle>>();
    m_wordClasses = wordClasses ? std::move(wordClasses) : std::make_shared<const dict::WordClassTable>();
    m_selections.fill(std::shared_ptr<const Selection>{});
    // Every entry was produced from the set being replaced, so none of them can answer a lookup
    // against the new one.
    m_cache.clear();
    m_cacheIndex.clear();
}

std::shared_ptr<const Engine::Selection>
Engine::classify(const dict::DictionarySnapshot &dictionaries,
                 const std::shared_ptr<const dict::WordClassTable> &wordClasses,
                 LookupCategory category)
{
    auto selection = std::make_shared<Selection>();
    selection->source = dictionaries;
    selection->wordClasses = wordClasses;

    for (const dict::DictionaryHandle &dictionary : *dictionaries) {
        selection->maxKeyLength = std::max(selection->maxKeyLength, dictionary.maxKeyLength);

        if (dict::isFrequencyType(dictionary.type)) {
            if (dictionary.type == dict::DictType::YomitanKanjiFrequency)
                selection->kanjiFrequencies.append(dictionary);
            else
                selection->frequencies.append(dictionary);
            continue;
        }
        if (dict::isPitchAccentType(dictionary.type)) {
            selection->pitchAccents.append(dictionary);
            continue;
        }
        if (dictionary.type == dict::DictType::KanjiComponents) {
            if (!selection->components.has_value())
                selection->components = dictionary;
            continue;
        }
        if (dictionary.type == dict::DictType::JMdict && !selection->examples.has_value())
            selection->examples = dictionary;

        // A dictionary the user excluded from the unfiltered lookup is still reachable through
        // the category of its own type, which is JL's NoAll option.
        if (category == LookupCategory::All && dictionary.options.excludeFromAll)
            continue;
        if (!dict::answersCategory(dictionary.type, category))
            continue;

        if (dict::isWordDictionaryType(dictionary.type))
            selection->words.append(dictionary);
        else if (dict::isNameDictionaryType(dictionary.type))
            selection->names.append(dictionary);
        else if (dict::isKanjiDictionaryType(dictionary.type))
            selection->kanji.append(dictionary);
    }
    return selection;
}

std::shared_ptr<const Engine::Selection> Engine::selectionFor(LookupCategory category) const
{
    const std::size_t index = categoryIndex(category);
    QMutexLocker locker(&m_mutex);
    if (index >= m_selections.size())
        return classify(m_dictionaries, m_wordClasses, category);
    if (!m_selections[index])
        m_selections[index] = classify(m_dictionaries, m_wordClasses, category);
    return m_selections[index];
}

void Engine::addKanjiCard(Result &result, const Selection &dictionaries)
{
    if (!result.kanjiRecord)
        return;

    if (dictionaries.examples.has_value()) {
        for (const dict::KanjiExample &example : dict::kanjiExamplesFor(*dictionaries.examples, result.primarySpelling))
            result.kanjiExamples.append(exampleText(example));
    }

    if (!dictionaries.components.has_value())
        return;
    for (const QString &component : dict::kanjiComponentsFor(*dictionaries.components, result.primarySpelling)) {
        // The component list holds characters alone; the meaning beside each of them comes from
        // the kanji dictionary the lookup already consults, so a component the dictionary does
        // not cover is shown bare.
        QString meaning;
        for (const dict::DictionaryHandle &kanjiDictionary : dictionaries.kanji) {
            for (const std::shared_ptr<const dict::Record> &record : dict::find(kanjiDictionary, component)) {
                const dict::KanjidicRecord *kanjidic = dict::asKanjidic(*record);
                if (kanjidic != nullptr && !kanjidic->definitions.isEmpty()) {
                    meaning = kanjidic->definitions.first();
                    break;
                }
            }
            if (!meaning.isEmpty())
                break;
        }
        result.kanjiComponents.append(meaning.isEmpty() ? component : component % u" "_s % meaning);
    }
}

QList<Result> Engine::lookupUncached(QStringView spanFromCursor,
                                     int maxResults,
                                     const Selection &dictionaries,
                                     const VariantOptions &variants,
                                     const QByteArray &substitutable) const
{
    const dict::WordClassTable &wordClasses = *dictionaries.wordClasses;

    const TextInfo info = buildTextInfo(m_rules, spanFromCursor);
    const Decorators decorators{.wordFrequencies = dictionaries.frequencies,
                                .kanjiFrequencies = dictionaries.kanjiFrequencies,
                                .pitchAccents = dictionaries.pitchAccents};

    QList<Result> results;
    for (const dict::DictionaryHandle &dictionary : dictionaries.words)
        results.append(queryWordDictionary(dictionary, info, wordClasses));
    for (const dict::DictionaryHandle &dictionary : dictionaries.names)
        results.append(queryNameDictionary(dictionary, info));

    if (!dictionaries.kanji.isEmpty()) {
        // The kanji card is built from the first character of the lookup text alone, exactly as
        // JL and meikipop do.
        const std::optional<QString> kanji = jp::firstCharacterIfKanji(spanFromCursor);
        if (kanji.has_value()) {
            for (const dict::DictionaryHandle &dictionary : dictionaries.kanji)
                results.append(queryKanjiDictionary(dictionary, *kanji));
        }
    }

    // Shared by the acceptance test of the variant pass and the decoration below, which probe the
    // frequency dictionaries for the same headwords.
    FrequencyMemo frequencies;

    QList<Result> variantResults;
    if (variants.enabled) {
        // Only a variant longer than every exact match is kept: an exact match of the same
        // length is the better-evidenced reading of the same characters.
        qsizetype longest = 0;
        for (const Result &result : std::as_const(results))
            longest = std::max(longest, result.matchedText.size());
        const TextInfo variantInfo = buildVariantTextInfo(m_rules, info, longest, variants, substitutable);
        if (!variantInfo.candidates.isEmpty()) {
            QList<Result> found;
            for (const dict::DictionaryHandle &dictionary : dictionaries.words)
                found.append(queryWordDictionary(dictionary, variantInfo, wordClasses));
            // Name variants are off by default because a substituted spelling can turn a correctly
            // recognized word into an unrelated personal name.
            if (variants.nameDictionaries) {
                for (const dict::DictionaryHandle &dictionary : dictionaries.names)
                    found.append(queryNameDictionary(dictionary, variantInfo));
            }
            for (Result &result : found) {
                const QList<FrequencyHit> &hits = frequencies.hitsFor(result, dictionaries.frequencies);
                if (!acceptsVariantResult(result, hits, variants))
                    continue;
                result.matchedByVariant = true;
                variantResults.append(std::move(result));
            }
        }
    }
    if (variants.rankFirst)
        results.append(std::exchange(variantResults, {}));

    // Three steps in place of one sort, because the decoration is what a large dictionary set
    // spends a lookup in and the popup reads it for maxResults results out of the whole list.
    // Over the 23-dictionary configuration of
    // research/implementation-notes/lookup-scaling.md, one lookup produced 96.5 results and the
    // decoration of all of them was 63 % of the call.
    //
    // First the cut on criteria 1 to 7, which read what the queries above already filled.
    // compareUndecorated() is a prefix of the full order, so a result strictly worse than the
    // maxResults-th best under it cannot enter the answer whatever the decoration says. The tie
    // run that touches the cut is kept, because criterion 8 decides inside it.
    //
    // With VariantOptions::rankFirst off, the exact results and the variant results are ordered
    // apart and the variant results follow, which is the same three steps over each list.
    const auto order = [&](QList<Result> &list) {
        if (maxResults > 0 && list.size() > maxResults) {
            std::ranges::stable_sort(list, lessThanUndecorated);
            qsizetype keep = maxResults;
            while (keep < list.size() && compareUndecorated(list.at(keep - 1), list.at(keep)) == 0)
                ++keep;
            list.erase(list.begin() + keep, list.end());
        }

        // Then the frequency half of the decoration, which criterion 8 reads, over every result whose
        // order is still undecided. A kanji result is decorated from the kanji frequency lists and
        // from the rank its own record carries, which is what dictionaries.kanji holding only
        // kanji-dictionary types makes the type test enough to select.
        for (Result &result : list) {
            if (dict::isKanjiDictionaryType(result.dictionary.type))
                decorateKanjiFrequencies(result, decorators);
            else
                decorateFrequencies(result, decorators, frequencies);
        }

        std::ranges::stable_sort(list, lessThan);
        if (maxResults > 0 && list.size() > maxResults)
            list.erase(list.begin() + maxResults, list.end());
    };
    order(results);
    if (!variantResults.isEmpty()) {
        order(variantResults);
        results.append(std::move(variantResults));
        if (maxResults > 0 && results.size() > maxResults)
            results.erase(results.begin() + maxResults, results.end());
    }

    // Last the pitch positions and the kanji card, which popup/ reads and ranking.cpp does not,
    // over the results the popup shows.
    for (Result &result : results) {
        decoratePitch(result, decorators);
        if (dict::isKanjiDictionaryType(result.dictionary.type))
            addKanjiCard(result, dictionaries);
    }

    qCDebug(logLookup) << results.size() << "results for" << spanFromCursor;
    return results;
}

QList<Result> Engine::lookupText(QStringView spanFromCursor,
                                 LookupCategory category,
                                 int maxResults,
                                 const Selection &dictionaries,
                                 const VariantOptions &variants,
                                 const QByteArray &substitutable) const
{
    if (spanFromCursor.isEmpty())
        return {};

    const CacheKey key{.dictionaries = dictionaries.source,
                       .span = spanFromCursor.toString(),
                       .category = category,
                       .maxResults = maxResults,
                       .variants = variants.enabled ? variants : VariantOptions{},
                       .substitutable = variants.enabled ? substitutable : QByteArray()};
    {
        QMutexLocker locker(&m_mutex);
        const auto position = m_cacheIndex.find(key);
        if (position != m_cacheIndex.end()) {
            m_cache.splice(m_cache.begin(), m_cache, position->second);
            return m_cache.front().second;
        }
    }

    QList<Result> results = lookupUncached(spanFromCursor, maxResults, dictionaries, key.variants, key.substitutable);

    QMutexLocker locker(&m_mutex);
    // Another thread may have cached the same span while this one was working; its entry is as
    // good as this one, so the newcomer replaces it rather than duplicating it.
    const auto existing = m_cacheIndex.find(key);
    if (existing != m_cacheIndex.end()) {
        m_cache.erase(existing->second);
        m_cacheIndex.erase(existing);
    }
    m_cache.emplace_front(key, results);
    m_cacheIndex[key] = m_cache.begin();
    while (static_cast<int>(m_cache.size()) > cacheCapacity) {
        m_cacheIndex.erase(m_cache.back().first);
        m_cache.pop_back();
    }
    return results;
}

QList<Result> Engine::lookupText(QStringView spanFromCursor, LookupCategory category, int maxResults) const
{
    // A span alone carries no confidences.
    VariantOptions variants = variantOptions();
    variants.enabled = variants.enabled && variants.withoutConfidences;
    return lookupText(spanFromCursor, category, maxResults, *selectionFor(category), variants, {});
}

Response Engine::lookup(const Request &request) const
{
    Response response;
    const QStringView text{request.sourceText};
    qsizetype cursor = request.cursorIndex;
    if (cursor < 0 || cursor >= text.size())
        return response;

    // A pointer over the trailing half of a surrogate pair is over the character the pair
    // spells, so the lookup starts one code unit earlier and the highlight with it.
    if (cursor > 0 && text.at(cursor).isLowSurrogate())
        --cursor;
    response.highlightStart = cursor;

    if (!jp::containsJapaneseCharacters(characterAt(text, cursor)))
        return response;

    // The one dictionary set this call runs against, taken once: the span clamp below and the
    // queries afterwards then agree about which dictionaries answered, whatever the GUI thread
    // publishes in the meantime.
    const std::shared_ptr<const Selection> dictionaries = selectionFor(request.category);

    int maxSearchLength = request.maxSearchLength > 0 ? request.maxSearchLength : text.size();
    // No store holds a key longer than this, so a longer span can only produce shorter
    // matches anyway, and every extra code unit costs a candidate.
    if (dictionaries->maxKeyLength > 0)
        maxSearchLength = std::min(maxSearchLength, dictionaries->maxKeyLength);

    qsizetype end = text.size();
    if (text.size() - cursor > maxSearchLength) {
        end = cursor + maxSearchLength;
        // A trailing high surrogate would split a UTF-16 pair at the length limit.
        // Exclude it; a trailing low surrogate completes a pair and must be kept.
        if (end > cursor && text.at(end - 1).isHighSurrogate())
            --end;
    }

    const QStringView searchSpan = text.left(end);
    const qsizetype boundary = jp::findExpressionBoundary(searchSpan, cursor);
    const QStringView span = searchSpan.sliced(cursor, boundary - cursor);
    if (span.isEmpty() || span.at(0).isSpace())
        return response;

    // The confidences of the span, as the substitution mask of lookup/ocrvariants.h. A request
    // whose list does not cover sourceText carries none, and a gate of 1 or more passes every
    // character, which the empty mask already says.
    VariantOptions variants = variantOptions();
    const bool scored = request.confidences.size() == text.size();
    variants.enabled = variants.enabled && (scored || variants.withoutConfidences);
    QByteArray substitutable;
    if (variants.enabled && variants.confidenceGate < 1.0F && scored) {
        substitutable.resize(span.size());
        for (qsizetype i = 0; i < span.size(); ++i)
            substitutable[i] = request.confidences.at(cursor + i) <= variants.confidenceGate ? 1 : 0;
    }
    response.results = lookupText(span, request.category, request.maxResults, *dictionaries, variants, substitutable);
    response.highlightLength = response.results.isEmpty() ? 0 : response.results.first().matchedText.size();
    return response;
}

void Engine::setVariantOptions(const VariantOptions &options)
{
    QMutexLocker locker(&m_mutex);
    m_variantOptions = options;
}

VariantOptions Engine::variantOptions() const
{
    QMutexLocker locker(&m_mutex);
    return m_variantOptions;
}

void Engine::invalidateCache()
{
    QMutexLocker locker(&m_mutex);
    m_cache.clear();
    m_cacheIndex.clear();
}

int Engine::cachedSpanCount() const
{
    QMutexLocker locker(&m_mutex);
    return static_cast<int>(m_cache.size());
}

} // namespace maru::lookup
