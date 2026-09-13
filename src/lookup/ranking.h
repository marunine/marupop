// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The fourteen-criterion result order.
//
// Ported from JL (Apache-2.0), JL.Core/Lookup/LookupResult.cs CompareTo and its score
// helpers; see NOTICE for the pinned upstream source. The first difference decides; the dominant key is the length of
// the matched span, which is what makes results[0].matchedText the longest match and therefore the highlight span.
#pragma once

#include "lookup/lookuptypes.h"

#include <QString>

namespace maru::lookup
{

// Criterion 6. 1 when the matched headword is written with outdated, irregular or rare kanji
// (oK, iK, rK), 0 when it is not, and INT_MAX when the result did not match on its headword at
// all, which parks it behind every result that did.
[[nodiscard]] int primarySpellingOrthographyScore(const Result &result);

// Criterion 7. 2 for an outdated, irregular or rare reading (ok, ik, rk), 0 for an entry marked
// "usually written in kana" (uk), 1 otherwise, and INT_MAX when the result did not match on one
// of its readings.
[[nodiscard]] int readingOrthographyScore(const Result &result);

// Criterion 8. The rank of the highest-priority frequency dictionary that covers the headword,
// inverted for an occurrence count so that a smaller score is always more common, and INT_MAX
// when no dictionary covers it. Falls back to the JMdict priority rank while no frequency
// dictionary is enabled, which decorate() signals by leaving Result::priorityRank set.
[[nodiscard]] int frequencyScore(const Result &result);

// The index of matchedText in readings, or -1. Criteria 3 and 10 read it.
[[nodiscard]] qsizetype readingIndexOfMatchedText(const Result &result);

// The plain-text definitions of the record, which criteria 13 and 14 compare. JL compares its
// pre-rendered FormattedDefinitions, tag brackets and all; marupop keeps glosses structured, so
// the comparison runs over the glosses alone.
[[nodiscard]] QString definitionText(const Result &result);

// Criteria 1 to 7, which are every criterion that reads a field the query stage already filled.
// Criterion 8 onward read Result::frequencies and Result::priorityRank, which decorate() attaches.
//
// The split is what lets a caller decorate fewer results than it found. This is a prefix of the
// full order, so a result strictly worse here than the n-th best cannot be among the n best of
// the full order whatever the decoration says, and never has to be decorated at all.
// Engine::lookupUncached() is the caller that uses it that way.
[[nodiscard]] int compareUndecorated(const Result &left, const Result &right);

// Negative, zero or positive as left sorts before, with or after right.
[[nodiscard]] int compareResults(const Result &left, const Result &right);

// The comparator std::stable_sort is called with.
[[nodiscard]] bool lessThan(const Result &left, const Result &right);

// The comparator for compareUndecorated(). A sort with it leaves results tied on criteria 1 to 7
// in the order they were found, so a later sort with lessThan() over the same list produces the
// order lessThan() alone would have.
[[nodiscard]] bool lessThanUndecorated(const Result &left, const Result &right);

} // namespace maru::lookup
