// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Japanese text preparation. Ported from JL (Apache-2.0), JL.Core/Japanese/JapaneseUtils.cs at
// commit 85ae02eeb84f378387f48c12e7b468390a9f2007, with the tables in jp/japanesetables.h generated from the same
// file. normalizeText() is the single entry point that turns raw text into the hiragana search
// key every dictionary index and every lookup candidate is keyed by.
#pragma once

#include <QList>
#include <QString>
#include <QStringView>

#include <optional>

namespace maru::jp
{

// The censoring mark every fuseji glyph folds to, U+25CB WHITE CIRCLE.
inline constexpr char16_t kNormalizedFuseji = 0x25CB;

// The hiragana search key for text: NFKC, ASCII uppercase, katakana to hiragana, variation
// selectors dropped, supplementary kyuujitai and hentaigana folded, the strip characters
// dropped in non-first and non-last position, iteration marks expanded, fuseji folded to
// kNormalizedFuseji, and a small tsu directly after a small tsu dropped.
//
// The QString overload returns its argument unchanged (one shared copy, no allocation) when
// the text already needs no work, which is the common case for a dictionary key.
[[nodiscard]] QString normalizeText(const QString &text);
[[nodiscard]] QString normalizeText(QStringView text);

// The number of elongation runs in text, capped at 4. A run starts at a long vowel mark or at
// a small vowel hiragana that continues the vowel of the preceding kana.
[[nodiscard]] int countNonConsecutiveLongVowelMarks(QStringView text);

// Every reading of text with each elongation run replaced by the vowel it lengthens. A run
// over お or え forks into both spellings (おお and おう, ええ and えい), so the result holds up
// to 2^n strings for n runs, in JL's order: the plain vowel first, the alternative second.
[[nodiscard]] QList<QString> normalizeLongVowelMark(QStringView text);

// Katakana U+30A0-U+30FF, Katakana Phonetic Extensions U+31F0-U+31FF and halfwidth katakana
// U+FF66-U+FF9D. JL tests U+30A0-U+31FF as one range; japanese.cpp states why the port omits
// U+3100-U+31EF.
[[nodiscard]] bool isKatakana(char32_t codePoint);

// The Hiragana block U+3040-U+309F. JL has no such predicate; the block is the counterpart of
// the katakana test above.
[[nodiscard]] bool isHiragana(char32_t codePoint);

// The kanji ranges of JL's two IsKanji overloads, unified over one code point.
[[nodiscard]] bool isKanji(char32_t codePoint);

// The ranges of JL's two IsJapaneseCharacter overloads, unified over one code point.
[[nodiscard]] bool isJapanese(char32_t codePoint);

// True when text holds at least one code point isJapanese() accepts.
[[nodiscard]] bool containsJapaneseCharacters(QStringView text);

// True when text holds at least one code unit in U+3040-U+30FF, U+31F0-U+31FF or
// U+FF66-U+FF9D.
[[nodiscard]] bool containsKana(QStringView text);

// The first character of text when it is a kanji, as a one code point string (two code units
// for an astral kanji). Empty text and a non-kanji first character both give nullopt.
[[nodiscard]] std::optional<QString> firstCharacterIfKanji(QStringView text);

// The index one past the first expression terminator at or after position, or text.size() when
// text holds none. The terminator set is every bracket of either side plus the sentence
// terminators.
[[nodiscard]] qsizetype findExpressionBoundary(QStringView text, qsizetype position);

// The sentence around position, trimmed, with one unmatched enclosing bracket pair removed.
[[nodiscard]] QString findSentence(QStringView text, qsizetype position);

// text split into visual characters: a small combining kana is glued onto the character before
// it. The views index into text, so text must outlive the result.
[[nodiscard]] QList<QStringView> combinedForm(QStringView text);

// The number of visual characters combinedForm() would produce, without building them.
[[nodiscard]] qsizetype combinedFormLength(QStringView text);

// Script conversion over the kana blocks alone: U+30A1-U+30F6 to U+3041-U+3096 plus the two
// iteration marks U+30FD and U+30FE. Unlike normalizeText(), ヰ and ヱ keep their identity as
// ゐ and ゑ, and nothing outside the katakana block changes.
[[nodiscard]] QString katakanaToHiragana(QStringView text);

// The inverse of katakanaToHiragana() over U+3041-U+3096, U+309D and U+309E.
[[nodiscard]] QString hiraganaToKatakana(QStringView text);

// Unicode general category P*, which is what the OCR backends classify a recognized glyph by.
[[nodiscard]] bool isPunctuation(char32_t codePoint);

} // namespace maru::jp
