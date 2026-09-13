// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The dictionaries that produce no results of their own and only annotate the results of the
// others: the frequency lists and the pitch-accent dictionary.
//
// Ported from JL (Apache-2.0) at commit 85ae02eeb84f378387f48c12e7b468390a9f2007: JL.Core/Lookup/LookupUtils.cs
// GetWordFrequencies, GetKanjiFrequencies and GetPitchPosition. The probe
// rules themselves live in dict/lookupsupport.h, which is where the store is reachable from.
#pragma once

#include "dict/dictionary.h"
#include "lookup/lookuptypes.h"

#include <QHash>
#include <QList>
#include <QString>

namespace maru::lookup
{

// The enabled decorator dictionaries of one lookup, in priority order. Handles by value, so the
// stores stay open for the whole decoration.
struct Decorators
{
    QList<dict::DictionaryHandle> wordFrequencies;
    QList<dict::DictionaryHandle> kanjiFrequencies;
    QList<dict::DictionaryHandle> pitchAccents;

    [[nodiscard]] bool isEmpty() const
    {
        return wordFrequencies.isEmpty() && kanjiFrequencies.isEmpty() && pitchAccents.isEmpty();
    }
};

// Frequency hits cached by headword for one lookup. dict::frequencyFor() reads only the
// headword and frequency stores, so repeated results for one headword share the same hits.
// The cache must not outlive the lookup's dictionary snapshot.
class FrequencyMemo
{
public:
    // The hits of result's headword against frequencies, probed on the first call per headword.
    [[nodiscard]] const QList<FrequencyHit> &hitsFor(const Result &result,
                                                     const QList<dict::DictionaryHandle> &frequencies);

private:
    QHash<QString, QList<FrequencyHit>> m_hits;
};

// Attaches one FrequencyHit per frequency dictionary that covers the headword, in priority order.
//
// A dictionary that does not cover the headword contributes nothing rather than a sentinel, so
// frequencies.first() is always the highest-priority dictionary that answered. JL instead keeps
// one int.MaxValue entry per list and drops the whole list when every entry is absent.
//
// Result::priorityRank is cleared while a word-frequency dictionary is enabled: it is the
// fallback the comparator uses in its place, and leaving both set would rank a JMdict headword
// by its priority band against another headword's real frequency.
//
// Criterion 8 of compareResults() reads what this attaches, so it runs over every result whose
// order is still undecided.
void decorateFrequencies(Result &result, const Decorators &decorators, FrequencyMemo &memo);

// The same for a kanji result: the kanji frequency lists, plus the KANJIDIC2 rank the record
// itself carries, which JL prepends as a synthetic entry. A lookup produces one kanji result per
// kanji dictionary, so this one takes no memo.
void decorateKanjiFrequencies(Result &result, const Decorators &decorators);

// Attaches the pitch position of each reading, from the first pitch-accent dictionary that
// answers for the headword.
//
// popup/popupview.cpp reads Result::pitchPositions and lookup/ranking.cpp reads none of it, so
// this runs over the results the popup shows rather than over every result found.
void decoratePitch(Result &result, const Decorators &decorators);

// decorateFrequencies() and decoratePitch() together, over a memo of this result alone. This is
// the whole decoration of one result, for a caller that has no list to amortize the memo over.
void decorate(Result &result, const Decorators &decorators);

// decorateKanjiFrequencies() and decoratePitch() together.
void decorateKanji(Result &result, const Decorators &decorators);

} // namespace maru::lookup
