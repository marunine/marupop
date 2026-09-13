// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Substitution variants of a search key, for the lookup pass LookupOcrVariants enables: the keys
// a text-recognition error of one character would have produced from a dictionary word.
//
// Three confusion sets supply candidate substitutions:
//
// - Voiced and semi-voiced kana (か/が, は/ば/ぱ, う/ゔ), and small and full-size kana (あ/ぁ, つ/っ),
//   in the hiragana key space. A search key is hiragana after jp::normalizeText(), so one table
//   covers katakana too. Substitutions work in both directions.
// - Lookalike kanji from similar-kanji (MIT, lookup/similarkanjitable.h): 2,902 kanji with a
//   median of 4 neighbours each, including 牛/生, 某/基 and 大/太.
// - Lookalikes across scripts (レ/し, ー/一, カ/力, ロ/口), keyed by the source character rather
//   than by the key character, since the key space folds katakana into hiragana and レ/し is not
//   れ/し.
#pragma once

#include "deconj/deconjugator.h"
#include "lookup/lookuptypes.h"
#include "lookup/textinfo.h"

#include <QByteArrayView>
#include <QList>
#include <QString>
#include <QStringView>

#include <cstdint>

namespace maru::lookup
{

// Which variant results the pass keeps. Every variant result has to match more text than every
// exact result; this decides which of those are shown.
enum class VariantAcceptance : std::uint8_t
{
    // A match that carries evidence of being a common word: written in katakana, tagged by
    // JMdict's priority lists, or ranked by a frequency dictionary. A dictionary set holding
    // neither JMdict nor a rank-ordered frequency dictionary can mark no word common, so there
    // only katakana matches are kept.
    CommonWords,
    // Every match.
    AnyWord,
};

// The settings of the pass. Every limit trades found words for time, so each is a setting
// (LookupVariant* in core/marupopsettings.kcfg) rather than a constant.
struct VariantOptions
{
    bool enabled = false;
    // A character read at a confidence above this is never substituted. 1 or more substitutes
    // every character, which is what a request carrying no confidences gets.
    float confidenceGate = 0.8F;
    // Whether the pass runs on a request that carries no confidences, which is every Chrome
    // Screen AI read. Such a request offers every character for substitution, increasing
    // the chance that a valid exact reading is replaced by a false match.
    bool withoutConfidences = false;
    // Candidates whose key is longer than this, in UTF-16 code units, get no variants; 0 is no
    // limit.
    int maxKeyLength = 12;
    // The variant keys one lookup probes at most, summed over every candidate; 0 is no limit.
    int maxKeys = 400;
    VariantAcceptance acceptance = VariantAcceptance::CommonWords;
    // Whether a match written entirely in hiragana, or two code units long and not two kanji, is
    // kept.
    bool shortAndHiraganaMatches = false;
    // Whether the name dictionaries are probed with the variants too.
    bool nameDictionaries = false;
    // Whether a variant result goes before the shorter exact results it outmatches, which makes
    // it the popup's first answer and its highlight. Off, every variant result is listed after
    // every exact one, so the pass adds entries to the list and never changes its first answer.
    bool rankFirst = true;

    [[nodiscard]] bool operator==(const VariantOptions &other) const = default;
};

// The JMdict priority rank that counts as evidence under VariantAcceptance::CommonWords: a class-1
// tag (ichi1, news1, spec1, gai1) or an nf band of 24 or better.
inline constexpr qint32 variantMaxPriorityRank = 12000;

// The rank in a rank-ordered frequency dictionary that counts as evidence under
// VariantAcceptance::CommonWords. A dictionary that counts occurrences, where a higher value is
// more frequent, has no rank to compare and is not evidence.
inline constexpr int variantMaxFrequencyRank = 20000;

// The characters keyCharacter, one character of a search key, can be a misread of, excluding
// itself: the other members of its kana voicing and small-kana groups for a hiragana key
// character, and its similar-kanji neighbours for a kanji. Empty for every other character.
[[nodiscard]] QString substitutesFor(QChar keyCharacter);

// The characters of another script sourceCharacter, one character of the recognized text, can be
// a misread of: ー for 一 and 一 for ー, 力 for カ, し for レ. Empty for every other character.
[[nodiscard]] QString lookalikesFor(QChar sourceCharacter);

// The key characters the position of a candidate can be substituted with: substitutesFor() the
// key character, then the key form of each lookalikesFor() the source character that is not
// already among them. sourceCharacter is null where the key does not align with the text.
[[nodiscard]] QString keySubstitutes(QChar keyCharacter, QChar sourceCharacter);

// The variant candidates of exact: for every candidate whose source text is longer than
// minTextLength and passes admitsVariants(), and whose key is at most options.maxKeyLength long,
// one Candidate per substitution variant, carrying the candidate's own source text, the
// deconjugation of the variant key and its elongation variants. Longest candidate first. Where the
// total exceeds options.maxKeys, the budget goes to the shortest candidates.
//
// A variant substituted inside the candidate's untouchedPrefix takes the candidate's own forms
// with the same character replaced, which is what deconjugating it would produce; a variant
// substituted anywhere else is deconjugated.
//
// substitutable holds one byte per code unit of the span the candidates were built from, nonzero
// where that character may be substituted; empty permits every character. It applies to a
// candidate whose key has the length of its source text, which is where key and text align
// character by character, and a candidate whose normalization changed the length is treated as
// having every character substitutable.
[[nodiscard]] TextInfo buildVariantTextInfo(const deconj::RuleSet &rules,
                                            const TextInfo &exact,
                                            qsizetype minTextLength,
                                            const VariantOptions &options,
                                            QByteArrayView substitutable = {});

// Whether source text can carry a variant result at all: 3 code units or more, or 2 kanji, and
// not written entirely in hiragana, unless options.shortAndHiraganaMatches is set. A hiragana key
// changed in one voiced character reaches some dictionary word from most correctly spelled
// spans. buildVariantTextInfo() skips a candidate that fails the test, so the test also bounds the
// cost of the pass.
[[nodiscard]] bool admitsVariants(QStringView text, const VariantOptions &options);

// Whether a result the variant pass found is kept. admitsVariants() has to hold for matchedText.
// Under VariantAcceptance::CommonWords the match and its headword also have to be written entirely
// in katakana, or the result has to carry a JMdict priority rank of variantMaxPriorityRank or
// better, or a rank of
// variantMaxFrequencyRank or better in one of frequencies, the hits of the word frequency
// dictionaries.
[[nodiscard]] bool
acceptsVariantResult(const Result &result, const QList<FrequencyHit> &frequencies, const VariantOptions &options);

} // namespace maru::lookup
