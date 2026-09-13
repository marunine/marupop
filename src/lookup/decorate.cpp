// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "decorate.h"

#include "dict/lookupsupport.h"
#include "dict/records.h"

#include <algorithm>

using namespace Qt::Literals::StringLiterals;

namespace maru::lookup
{

namespace
{

// The two fields dict::frequencyFor() reads, as one string. U+001F separates the spelling from
// the readings and U+001E the readings from each other, because neither appears in a headword.
[[nodiscard]] QString headwordKey(const Result &result)
{
    return result.primarySpelling % u"\x1f"_s % result.readings.join(u'\x1e');
}

[[nodiscard]] QList<FrequencyHit> hitsOf(const Result &result, const QList<dict::DictionaryHandle> &frequencies)
{
    QList<FrequencyHit> hits;
    for (const dict::DictionaryHandle &frequency : frequencies) {
        const std::optional<int> rank = dict::frequencyFor(frequency, result.primarySpelling, result.readings);
        if (rank.has_value()) {
            hits.append(FrequencyHit{.dictionaryName = frequency.name,
                                     .rank = *rank,
                                     .higherIsBetter = frequency.options.higherValueMeansHigherFrequency});
        }
    }
    return hits;
}

} // namespace

const QList<FrequencyHit> &FrequencyMemo::hitsFor(const Result &result,
                                                  const QList<dict::DictionaryHandle> &frequencies)
{
    const QString key = headwordKey(result);
    const auto cached = m_hits.constFind(key);
    if (cached != m_hits.constEnd())
        return *cached;
    return *m_hits.insert(key, hitsOf(result, frequencies));
}

void decorateFrequencies(Result &result, const Decorators &decorators, FrequencyMemo &memo)
{
    result.frequencies.append(memo.hitsFor(result, decorators.wordFrequencies));
    if (!decorators.wordFrequencies.isEmpty())
        result.priorityRank = 0;
}

void decorateKanjiFrequencies(Result &result, const Decorators &decorators)
{
    if (result.kanjiRecord) {
        if (const dict::KanjidicRecord *record = dict::asKanjidic(*result.kanjiRecord);
            record != nullptr && record->frequency > 0) {
            // JL prepends a synthetic entry named KANJIDIC2; the dictionary's own name is what
            // the user configured and what the popup shows beside the rank.
            result.frequencies.append(FrequencyHit{
                .dictionaryName = result.dictionary.name, .rank = record->frequency, .higherIsBetter = false});
        }
    }

    for (const dict::DictionaryHandle &frequency : decorators.kanjiFrequencies) {
        const std::optional<int> rank = dict::frequencyFor(frequency, result.primarySpelling, {});
        if (rank.has_value())
            result.frequencies.append(
                FrequencyHit{.dictionaryName = frequency.name,
                             .rank = *rank,
                             .higherIsBetter = frequency.options.higherValueMeansHigherFrequency});
    }
    result.priorityRank = 0;
}

void decoratePitch(Result &result, const Decorators &decorators)
{
    for (const dict::DictionaryHandle &pitch : decorators.pitchAccents) {
        const QList<std::optional<quint8>> positions =
            dict::pitchPositionsFor(pitch, result.primarySpelling, result.readings);
        const bool answered = std::ranges::any_of(positions, [](const std::optional<quint8> &position) {
            return position.has_value();
        });
        if (answered) {
            result.pitchPositions = positions;
            return;
        }
    }
}

void decorate(Result &result, const Decorators &decorators)
{
    FrequencyMemo memo;
    decorateFrequencies(result, decorators, memo);
    decoratePitch(result, decorators);
}

void decorateKanji(Result &result, const Decorators &decorators)
{
    decorateKanjiFrequencies(result, decorators);
    decoratePitch(result, decorators);
}

} // namespace maru::lookup
