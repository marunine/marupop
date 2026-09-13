// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Context-sensitive post-recognition rewrites for meikiocr lines. The operational
// specification is tools/data/meiki-corrections.json; see tools/data/README.md.
// Confidence thresholds assume punctuation factor 0.2 and recognition threshold 0.1.
#pragma once

#include "ocr/ocrtypes.h"

#include <QChar>

#include <array>
#include <optional>
#include <span>
#include <string_view>

namespace maru::ocr
{

// One context token of a rule: one literal character, one character class, or the line edge
// (BOS before the occurrence, EOS after it).
struct CorrectionToken
{
    enum class Kind : quint8
    {
        Literal,
        Hiragana,
        Katakana,
        Kanji,
        Digit,
        Latin,
        Space,
        Other,
        LineEdge,
    };

    Kind kind = Kind::Literal;
    char16_t character = 0;
};

enum class CorrectionMode : quint8
{
    Any,
    Horizontal,
    Vertical,
};

// One rewrite. before holds beforeCount tokens, nearest last; after holds afterCount tokens,
// nearest first. The rule fires where every character of the occurrence of find has a confidence
// at or under maxConfidence; an empty maxConfidence fires at any confidence.
struct CorrectionRule
{
    std::u16string_view find;
    std::u16string_view replace;
    std::array<CorrectionToken, 2> before{};
    int beforeCount = 0;
    std::array<CorrectionToken, 2> after{};
    int afterCount = 0;
    CorrectionMode mode = CorrectionMode::Any;
    std::optional<double> maxConfidence;
};

// The character class represented as a token kind: Hiragana through
// Other, never Literal or LineEdge.
[[nodiscard]] CorrectionToken::Kind correctionClassOf(QChar character);

// The 74 rules of ocr/meikicorrectiontable.h, in application order.
[[nodiscard]] std::span<const CorrectionRule> meikiCorrectionRules();

// Applies rules to line in order. Each rule's occurrences are found left to right and
// non-overlapping on the text the earlier rules left. A replacement of the same length keeps each
// character's box; one of another length splits the union of the replaced boxes equally along the
// reading axis, and a deletion drops the boxes. Every replaced character takes the lowest
// confidence of the span it replaces. line.box and line.confidence are recomputed from the
// remaining characters, and line.text.size() == line.chars.size() holds on return.
// Returns the number of rules that fired.
int applyCorrections(TextLine &line, std::span<const CorrectionRule> rules);

// applyCorrections() with meikiCorrectionRules().
int applyMeikiCorrections(TextLine &line);

} // namespace maru::ocr
