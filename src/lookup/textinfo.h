// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Candidate generation: the prefixes of the text under the cursor, their search keys and every
// dictionary form the deconjugator reaches from them.
//
// Ported from JL (Apache-2.0), JL.Core/Lookup/LookupUtils.cs GetTextInfo;
// see NOTICE for the pinned upstream source. The three
// guards are JL's: no deconjugation of a key longer than 35 code units, none of a lone fuseji,
// and chōonpu variants only for a key of at most 20 code units holding one to three elongation
// runs.
#pragma once

#include "deconj/deconjugator.h"

#include <QList>
#include <QString>
#include <QStringView>

#include <vector>

namespace maru::lookup
{

// One prefix of the span from the cursor, longest first.
struct Candidate
{
    // The raw source characters, which is what a Result carries as matchedText and what the
    // popup highlights. Never the normalized key: normalization changes length.
    QString text;
    // jp::normalizeText(text), the key every store is probed with.
    QString key;
    // The dictionary forms key can be a conjugation of. Empty when a guard skipped the search.
    std::vector<deconj::Form> forms;
    // How many leading code units of key no deconjugation rule read (deconj::deconjugate()), and
    // key.size() where a guard skipped the search.
    qsizetype untouchedPrefix = 0;
    // key with its elongation runs resolved, in jp::normalizeLongVowelMark() order, and the
    // deconjugation of each. Both lists are index-parallel and empty for a key holding no
    // elongation run.
    QList<QString> longVowelVariants;
    std::vector<std::vector<deconj::Form>> longVowelVariantForms;
};

// Every candidate of one lookup, plus the flattened lemma set.
struct TextInfo
{
    // Longest prefix first, one per code point boundary. A surrogate pair is never split.
    QList<Candidate> candidates;
    // Every distinct lemma across every candidate and every chōonpu variant, in first-seen
    // order. JL uses it to batch one database query per dictionary; marupop probes the store
    // per key, so it serves the tests and the deconjugated-key statistics.
    QList<QString> deconjugatedTexts;
};

// The candidates of textFromCursor. rules has to outlive the result: Form::lastTag is a view
// into the rule set's string arena.
[[nodiscard]] TextInfo buildTextInfo(const deconj::RuleSet &rules, QStringView textFromCursor);

// Fills candidate.longVowelVariants and candidate.longVowelVariantForms from candidate.text and
// candidate.key under JL's guards. Returns the elongation runs key holds, capped at 4, or -1 where
// a guard skipped the count: a text opening on an elongation mark, or a key over 20 code units.
int addLongVowelVariants(const deconj::RuleSet &rules, Candidate &candidate);

} // namespace maru::lookup
