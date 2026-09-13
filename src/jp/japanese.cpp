// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Ported from JL (Apache-2.0), JL.Core/Japanese/JapaneseUtils.cs at commit 85ae02eeb84f378387f48c12e7b468390a9f2007.
#include "japanese.h"

#include "core/logging.h"
#include "japanesetables.h"

#include <QChar>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace maru::jp
{

using tables::CharPair;
using tables::SupplementaryPair;

namespace
{

// U+3099 COMBINING KATAKANA-HIRAGANA VOICED SOUND MARK and U+309A, the semi-voiced one. An
// iteration mark directly after one of them repeats the base character together with the mark.
constexpr char16_t kCombiningVoiced = 0x3099;
constexpr char16_t kCombiningSemiVoiced = 0x309A;
constexpr char16_t kHiraganaSmallTsu = 0x3063;
constexpr char16_t kHiraganaIterationMarkDakuten = 0x309E;

constexpr char16_t kVariationSelectorFirst = 0xFE00;
constexpr char16_t kVariationSelectorLast = 0xFE0F;
constexpr char16_t kVariationSelectorSupplementHighSurrogate = 0xDB40;
constexpr char16_t kVariationSelectorSupplementLowFirst = 0xDD00;
constexpr char16_t kVariationSelectorSupplementLowLast = 0xDDEF;

// The elongation runs normalizeLongVowelMark() forks on. JL caps the count at 4 because the
// variant count is 2^n and the lookup pipeline skips a candidate at 4 runs anyway.
constexpr int kMaxLongVowelMarkCount = 4;

template <std::size_t N>
[[nodiscard]] constexpr bool contains(const std::array<char16_t, N> &sorted, char16_t value)
{
    return std::ranges::binary_search(sorted, value);
}

[[nodiscard]] std::optional<char16_t> mapChar(std::span<const CharPair> sorted, char16_t value)
{
    const auto it = std::ranges::lower_bound(sorted, value, std::less<>{}, &CharPair::from);
    if (it == sorted.end() || it->from != value)
        return std::nullopt;
    return it->to;
}

[[nodiscard]] std::optional<char16_t> mapSupplementary(char32_t codePoint)
{
    const auto *const it = std::ranges::lower_bound(
        tables::kSupplementaryNormalization, codePoint, std::less<>{}, &SupplementaryPair::from);
    if (it == tables::kSupplementaryNormalization.end() || it->from != codePoint)
        return std::nullopt;
    return it->to;
}

// The fast bail of normalizeText() tests every code unit of the input against a 592-entry set,
// so the set is materialized as an 8 KiB bitmap over the BMP rather than binary-searched.
using NormalizeBitmap = std::array<std::uint64_t, 1024>;

constexpr NormalizeBitmap buildNormalizeBitmap()
{
    NormalizeBitmap bitmap{};
    for (const char16_t value : tables::kCharactersToNormalize)
        bitmap[value / 64] |= std::uint64_t{1} << (value % 64);
    return bitmap;
}

constexpr NormalizeBitmap kNormalizeBitmap = buildNormalizeBitmap();

[[nodiscard]] constexpr bool needsNormalization(char16_t value)
{
    return (kNormalizeBitmap[value / 64] & (std::uint64_t{1} << (value % 64))) != 0;
}

[[nodiscard]] constexpr bool isIterationMark(char16_t value)
{
    return value == u'々' || value == u'〻' || value == u'ゝ' || value == kHiraganaIterationMarkDakuten;
}

[[nodiscard]] bool isRepeatedSmallTsu(const QString &built, char16_t value)
{
    return value == kHiraganaSmallTsu && !built.isEmpty() && built.back().unicode() == kHiraganaSmallTsu;
}

// Repeats the previously emitted character. U+309E adds a dakuten to it; the other three marks
// repeat it verbatim, carrying a surrogate pair or a combining voiced mark along with it. With
// nothing emitted yet the mark itself is kept, which is what leaves a leading 々 in place.
void appendIterationMark(QString &built, char16_t iterationMark)
{
    if (built.isEmpty()) {
        built.append(QChar(iterationMark));
        return;
    }

    const char16_t previous = built.back().unicode();
    if (iterationMark == kHiraganaIterationMarkDakuten) {
        const std::optional<char16_t> voiced = mapChar(tables::kHiraganaToDakuten, previous);
        built.append(QChar(voiced.value_or(iterationMark)));
        return;
    }

    if (QChar::isLowSurrogate(previous) && built.size() > 1) {
        const QChar high = built.at(built.size() - 2);
        built.append(high);
        built.append(QChar(previous));
    } else if (previous != kCombiningVoiced && previous != kCombiningSemiVoiced) {
        built.append(QChar(previous));
    } else if (built.size() > 1) {
        const QChar base = built.at(built.size() - 2);
        if (base.isLowSurrogate()) {
            built.append(QChar(iterationMark));
        } else {
            built.append(base);
            built.append(QChar(previous));
        }
    } else {
        built.append(QChar(iterationMark));
    }
}

// お lengthens to both おお and おう, え to both ええ and えい. Every other vowel has one
// spelling, reported as U+0000.
[[nodiscard]] constexpr char16_t alternativeVowel(char16_t vowel)
{
    if (vowel == u'お')
        return u'う';
    if (vowel == u'え')
        return u'い';
    return u'\0';
}

struct Elongation
{
    bool triggered;
    char16_t vowel;
    char16_t alternative;
};

[[nodiscard]] Elongation elongationTrigger(char16_t current, char16_t previous)
{
    const std::optional<char16_t> vowel = mapChar(tables::kKanaFinalVowel, previous);
    if (!vowel)
        return {.triggered = false, .vowel = u'\0', .alternative = u'\0'};

    const char16_t alternative = alternativeVowel(*vowel);
    if (contains(tables::kLongVowelMarks, current))
        return {.triggered = true, .vowel = *vowel, .alternative = alternative};

    const std::optional<char16_t> smallVowel = mapChar(tables::kSmallVowelHiraganaToFinalVowel, current);
    const bool triggered = smallVowel && (*smallVowel == *vowel || *smallVowel == alternative);
    return {.triggered = triggered, .vowel = *vowel, .alternative = alternative};
}

[[nodiscard]] bool isElongationContinuation(char16_t value, char16_t vowel, char16_t alternative)
{
    if (contains(tables::kLongVowelMarks, value))
        return true;
    const std::optional<char16_t> smallVowel = mapChar(tables::kSmallVowelHiraganaToFinalVowel, value);
    return smallVowel && (*smallVowel == vowel || *smallVowel == alternative);
}

[[nodiscard]] bool isLongVowelMarkOrSmallVowel(char16_t value)
{
    return contains(tables::kLongVowelMarks, value) ||
           mapChar(tables::kSmallVowelHiraganaToFinalVowel, value).has_value();
}

// The right-hand side of tables::kLeftToRightBracket, searched by the closing bracket. The
// table is sorted by the opening bracket, so the reverse direction is a linear scan over 28
// entries rather than a second sorted table.
[[nodiscard]] std::optional<char16_t> openingBracketFor(char16_t closing)
{
    for (const CharPair &pair : tables::kLeftToRightBracket) {
        if (pair.to == closing)
            return pair.from;
    }
    return std::nullopt;
}

[[nodiscard]] bool isExpressionTerminator(char16_t value)
{
    if (contains(tables::kSentenceTerminators, value))
        return true;
    if (mapChar(tables::kLeftToRightBracket, value))
        return true;
    return openingBracketFor(value).has_value();
}

[[nodiscard]] qsizetype countOccurrences(QStringView text, char16_t value)
{
    return std::count(text.begin(), text.end(), QChar(value));
}

} // namespace

QString normalizeText(const QString &text)
{
    // NFKC folds ＯＬ to OL, the fullwidth space to a halfwidth one, ｶﾞ to ガ, ﾜ to ワ, ㍿ to
    // 株式会社, U+FF5E to ~, and the Kangxi radicals to their kanji. QString::normalized()
    // returns a shared copy of its argument when the text is already normalized.
    QString normalized = text.normalized(QString::NormalizationForm_KC);

    // JL calls ToUpperInvariant() here, which uppercases every lowercase letter of any script.
    // This pass is ASCII-only: the keys it exists for are the
    // romaji abbreviations JMdict carries (vs to VS, h to H).
    bool hasLowerAscii = false;
    for (const QChar character : normalized) {
        if (character.unicode() >= u'a' && character.unicode() <= u'z') {
            hasLowerAscii = true;
            break;
        }
    }
    if (hasLowerAscii) {
        QString uppercased = normalized;
        for (qsizetype i = 0; i < uppercased.size(); ++i) {
            const char16_t value = uppercased.at(i).unicode();
            if (value >= u'a' && value <= u'z')
                uppercased[i] = QChar(static_cast<char16_t>(value - 0x20));
        }
        normalized = uppercased;
    }

    qsizetype start = -1;
    for (qsizetype i = 0; i < normalized.size(); ++i) {
        if (needsNormalization(normalized.at(i).unicode())) {
            start = i;
            break;
        }
    }
    if (start < 0)
        return normalized;

    const qsizetype length = normalized.size();
    QString built;
    built.reserve(length);
    built.append(QStringView{normalized}.first(start));

    for (qsizetype i = start; i < length; ++i) {
        const char16_t character = normalized.at(i).unicode();
        if (character >= kVariationSelectorFirst && character <= kVariationSelectorLast)
            continue;

        const bool nonLastChar = i + 1 < length;
        if (nonLastChar) {
            if (QChar::isHighSurrogate(character)) {
                const char16_t next = normalized.at(i + 1).unicode();
                if (character == kVariationSelectorSupplementHighSurrogate &&
                    next >= kVariationSelectorSupplementLowFirst && next <= kVariationSelectorSupplementLowLast) {
                    ++i;
                    continue;
                }

                const std::optional<char16_t> supplementary = mapSupplementary(QChar::surrogateToUcs4(character, next));
                if (supplementary) {
                    built.append(QChar(*supplementary));
                } else {
                    built.append(QChar(character));
                    built.append(QChar(next));
                }
                ++i;
                continue;
            }

            // The strip pass skips the first and the last code unit on purpose, which is what
            // leaves a trailing ・ in place.
            if (i > 0 && contains(tables::kCharsToStrip, character))
                continue;
        }

        const std::optional<char16_t> mapped = mapChar(tables::kNormalization, character);
        const char16_t value = mapped.value_or(character);
        if (isIterationMark(value)) {
            appendIterationMark(built, value);
        } else if (!mapped && contains(tables::kFuseji, value)) {
            built.append(QChar(kNormalizedFuseji));
        } else if (!isRepeatedSmallTsu(built, value)) {
            built.append(QChar(value));
        }
    }

    return built;
}

QString normalizeText(QStringView text)
{
    return normalizeText(text.toString());
}

int countNonConsecutiveLongVowelMarks(QStringView text)
{
    int count = 0;
    qsizetype searchStart = 0;

    while (searchStart < text.size()) {
        qsizetype candidate = -1;
        for (qsizetype i = searchStart; i < text.size(); ++i) {
            if (isLongVowelMarkOrSmallVowel(text.at(i).unicode())) {
                candidate = i;
                break;
            }
        }
        if (candidate < 0)
            break;

        const Elongation elongation =
            candidate == 0 ? Elongation{.triggered = false, .vowel = u'\0', .alternative = u'\0'}
                           : elongationTrigger(text.at(candidate).unicode(), text.at(candidate - 1).unicode());
        if (!elongation.triggered) {
            searchStart = candidate + 1;
            continue;
        }

        ++count;
        if (count == kMaxLongVowelMarkCount)
            break;

        qsizetype runEnd = candidate + 1;
        while (runEnd < text.size() &&
               isElongationContinuation(text.at(runEnd).unicode(), elongation.vowel, elongation.alternative)) {
            ++runEnd;
        }
        searchStart = runEnd;
    }

    return count;
}

QList<QString> normalizeLongVowelMark(QStringView text)
{
    if (text.isEmpty()) {
        qCWarning(logJp, "normalizeLongVowelMark called with empty text");
        return {};
    }

    QList<QString> variants;
    variants.append(QString(text.at(0)));

    for (qsizetype i = 1; i < text.size(); ++i) {
        const char16_t character = text.at(i).unicode();
        const Elongation elongation = isLongVowelMarkOrSmallVowel(character)
                                          ? elongationTrigger(character, text.at(i - 1).unicode())
                                          : Elongation{.triggered = false, .vowel = u'\0', .alternative = u'\0'};

        if (!elongation.triggered) {
            for (QString &variant : variants)
                variant.append(QChar(character));
            continue;
        }

        while (i + 1 < text.size() &&
               isElongationContinuation(text.at(i + 1).unicode(), elongation.vowel, elongation.alternative)) {
            ++i;
        }

        if (elongation.alternative == u'\0') {
            for (QString &variant : variants)
                variant.append(QChar(elongation.vowel));
            continue;
        }

        // The fork doubles the variant list: the original half takes the plain vowel, the
        // appended half the alternative one, which is the order JL's KanaTests pins for オー.
        const qsizetype half = variants.size();
        variants.reserve(half * 2);
        for (qsizetype j = 0; j < half; ++j)
            variants.append(variants.at(j));
        for (qsizetype j = 0; j < variants.size(); ++j)
            variants[j].append(QChar(j < half ? elongation.vowel : elongation.alternative));
    }

    return variants;
}

// Accept only the Katakana, Katakana Phonetic Extensions and halfwidth katakana
// blocks. Keeping the intervening Unicode blocks out prevents Bopomofo, Hangul,
// Kanbun and CJK strokes from passing the OCR Japanese-language filter.
bool isKatakana(char32_t codePoint)
{
    return (codePoint >= 0x30A0 && codePoint <= 0x30FF) || (codePoint >= 0x31F0 && codePoint <= 0x31FF) ||
           (codePoint >= 0xFF66 && codePoint <= 0xFF9D);
}

bool isHiragana(char32_t codePoint)
{
    return codePoint >= 0x3040 && codePoint <= 0x309F;
}

bool isKanji(char32_t codePoint)
{
    return (codePoint >= 0x4E00 && codePoint <= 0x9FFF)       // CJK Unified Ideographs
           || (codePoint >= 0x2E80 && codePoint <= 0x2FDF)    // CJK Radicals Supplement, Kangxi Radicals
           || (codePoint >= 0x3190 && codePoint <= 0x319F)    // Kanbun
           || (codePoint >= 0x3220 && codePoint <= 0x325F)    // Enclosed CJK Letters and Months
           || (codePoint >= 0x3280 && codePoint <= 0x4DBF)    // Enclosed CJK, CJK Compatibility, Extension A
           || (codePoint >= 0xF900 && codePoint <= 0xFAFF)    // CJK Compatibility Ideographs
           || (codePoint >= 0xFE10 && codePoint <= 0xFE1F)    // Vertical Forms
           || (codePoint >= 0xFE30 && codePoint <= 0xFE4F)    // CJK Compatibility Forms
           || (codePoint >= 0x1D360 && codePoint <= 0x1D37F)  // Counting Rod Numerals
           || (codePoint >= 0x1F200 && codePoint <= 0x1F2FF)  // Enclosed Ideographic Supplement
           || (codePoint >= 0x20000 && codePoint <= 0x2A6DF)  // Extension B
           || (codePoint >= 0x2A700 && codePoint <= 0x2EBEF)  // Extensions C, D, E, F
           || (codePoint >= 0x2F800 && codePoint <= 0x2FA1F)  // CJK Compatibility Ideographs Supplement
           || (codePoint >= 0x30000 && codePoint <= 0x3347F); // Extensions G, H, J
}

bool isJapanese(char32_t codePoint)
{
    return (codePoint >= 0x2FF0 && codePoint <= 0x30FF)       // Ideographic Description, CJK Symbols, kana
           || (codePoint >= 0x4E00 && codePoint <= 0x9FFF)    // CJK Unified Ideographs
           || codePoint == 0x00D7                             // MULTIPLICATION SIGN, a fuseji
           || (codePoint >= 0x2000 && codePoint <= 0x206F)    // General Punctuation
           || (codePoint >= 0x25A0 && codePoint <= 0x25FF)    // Geometric Shapes
           || (codePoint >= 0x2E80 && codePoint <= 0x2FDF)    // CJK Radicals Supplement, Kangxi Radicals
           || (codePoint >= 0x3190 && codePoint <= 0x319F)    // Kanbun
           || (codePoint >= 0x31C0 && codePoint <= 0x325F)    // CJK Strokes, Katakana Phonetic Extensions
           || (codePoint >= 0x3280 && codePoint <= 0x4DBF)    // Enclosed CJK, CJK Compatibility, Extension A
           || (codePoint >= 0xF900 && codePoint <= 0xFAFF)    // CJK Compatibility Ideographs
           || (codePoint >= 0xFE10 && codePoint <= 0xFE1F)    // Vertical Forms
           || (codePoint >= 0xFE30 && codePoint <= 0xFE4F)    // CJK Compatibility Forms
           || (codePoint >= 0xFF00 && codePoint <= 0xFF9F)    // Halfwidth and Fullwidth Forms
           || (codePoint >= 0xFFE0 && codePoint <= 0xFFEF)    // Halfwidth and Fullwidth Forms
           || (codePoint >= 0x1B000 && codePoint <= 0x1B16F)  // Kana Supplement, Extended-A, Small Kana
           || (codePoint >= 0x1D360 && codePoint <= 0x1D37F)  // Counting Rod Numerals
           || (codePoint >= 0x1F200 && codePoint <= 0x1F2FF)  // Enclosed Ideographic Supplement
           || (codePoint >= 0x20000 && codePoint <= 0x2A6DF)  // Extension B
           || (codePoint >= 0x2A700 && codePoint <= 0x2EBEF)  // Extensions C, D, E, F
           || (codePoint >= 0x2F800 && codePoint <= 0x2FA1F)  // CJK Compatibility Ideographs Supplement
           || (codePoint >= 0x30000 && codePoint <= 0x3347F); // Extensions G, H, J
}

bool containsJapaneseCharacters(QStringView text)
{
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar character = text.at(i);
        if (!character.isHighSurrogate()) {
            if (isJapanese(character.unicode()))
                return true;
            continue;
        }

        if (i + 1 >= text.size())
            return false;
        if (isJapanese(QChar::surrogateToUcs4(character, text.at(i + 1))))
            return true;
        ++i;
    }

    return false;
}

// Hiragana plus the katakana blocks; exclude the intervening non-kana blocks
// for the same language-filtering reason as isKatakana().
bool containsKana(QStringView text)
{
    return std::ranges::any_of(text, [](QChar character) {
        const char16_t value = character.unicode();
        return (value >= 0x3040 && value <= 0x30FF) || (value >= 0x31F0 && value <= 0x31FF) ||
               (value >= 0xFF66 && value <= 0xFF9D);
    });
}

std::optional<QString> firstCharacterIfKanji(QStringView text)
{
    if (text.isEmpty())
        return std::nullopt;

    const QChar first = text.at(0);
    if (!first.isHighSurrogate())
        return isKanji(first.unicode()) ? std::optional<QString>(QString(first)) : std::nullopt;

    if (text.size() < 2)
        return std::nullopt;

    const QChar second = text.at(1);
    if (!isKanji(QChar::surrogateToUcs4(first, second)))
        return std::nullopt;
    return text.first(2).toString();
}

qsizetype findExpressionBoundary(QStringView text, qsizetype position)
{
    for (qsizetype i = position; i < text.size(); ++i) {
        if (isExpressionTerminator(text.at(i).unicode()))
            return i + 1;
    }
    return text.size();
}

QString findSentence(QStringView text, qsizetype position)
{
    if (text.isEmpty())
        return {};

    qsizetype startPosition = -1;
    qsizetype endPosition = -1;
    const qsizetype clamped = std::clamp<qsizetype>(position, 0, text.size());

    for (const char16_t terminator : tables::kSentenceTerminators) {
        for (qsizetype i = clamped - 1; i >= 0; --i) {
            if (text.at(i).unicode() == terminator) {
                startPosition = std::max(startPosition, i);
                break;
            }
        }
        for (qsizetype i = clamped; i < text.size(); ++i) {
            if (text.at(i).unicode() == terminator) {
                if (endPosition < 0 || i < endPosition)
                    endPosition = i;
                break;
            }
        }
    }

    ++startPosition;
    if (endPosition < 0)
        endPosition = text.size() - 1;

    QStringView sentence = startPosition <= endPosition
                               ? text.sliced(startPosition, endPosition - startPosition + 1).trimmed()
                               : QStringView();
    if (sentence.size() <= 1)
        return sentence.toString();

    // One unmatched enclosing bracket is dropped from each side, then a fully matched pair is
    // unwrapped. The counting branches keep a nested pair such as 『…』 inside the sentence.
    if (openingBracketFor(sentence.at(0).unicode()))
        sentence = sentence.sliced(1);

    if (!sentence.isEmpty() && mapChar(tables::kLeftToRightBracket, sentence.back().unicode()))
        sentence.chop(1);

    if (sentence.isEmpty())
        return {};

    if (const std::optional<char16_t> closing = mapChar(tables::kLeftToRightBracket, sentence.at(0).unicode())) {
        if (sentence.back().unicode() == *closing) {
            sentence = sentence.sliced(1, sentence.size() - 2);
        } else if (countOccurrences(sentence, *closing) == 0) {
            sentence = sentence.sliced(1);
        } else {
            const char16_t opening = sentence.at(0).unicode();
            if (countOccurrences(sentence, opening) == countOccurrences(sentence, *closing) + 1)
                sentence = sentence.sliced(1);
        }
    } else if (const std::optional<char16_t> opening = openingBracketFor(sentence.back().unicode())) {
        // JL writes these as two branches with the same body: the closing bracket goes when the
        // sentence holds no matching opening bracket at all, and when it holds exactly one more
        // closing bracket than opening ones.
        const char16_t closing = sentence.back().unicode();
        const qsizetype openings = countOccurrences(sentence, *opening);
        if (openings == 0 || countOccurrences(sentence, closing) == openings + 1)
            sentence.chop(1);
    }

    return sentence.toString();
}

QList<QStringView> combinedForm(QStringView text)
{
    QList<QStringView> units;
    units.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (i + 1 < text.size() && contains(tables::kSmallCombiningKana, text.at(i + 1).unicode())) {
            units.append(text.sliced(i, 2));
            ++i;
        } else {
            units.append(text.sliced(i, 1));
        }
    }
    return units;
}

qsizetype combinedFormLength(QStringView text)
{
    qsizetype length = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        ++length;
        if (i + 1 < text.size() && contains(tables::kSmallCombiningKana, text.at(i + 1).unicode()))
            ++i;
    }
    return length;
}

QString katakanaToHiragana(QStringView text)
{
    QString converted = text.toString();
    for (qsizetype i = 0; i < converted.size(); ++i) {
        // U+30A1-U+30F6 are the katakana whose hiragana counterpart sits 0x60 lower, and
        // U+30FD and U+30FE are the two katakana iteration marks, at the same distance.
        const char16_t value = converted.at(i).unicode();
        if ((value >= 0x30A1 && value <= 0x30F6) || value == 0x30FD || value == 0x30FE)
            converted[i] = QChar(static_cast<char16_t>(value - 0x60));
    }
    return converted;
}

QString hiraganaToKatakana(QStringView text)
{
    QString converted = text.toString();
    for (qsizetype i = 0; i < converted.size(); ++i) {
        const char16_t value = converted.at(i).unicode();
        if ((value >= 0x3041 && value <= 0x3096) || value == 0x309D || value == 0x309E)
            converted[i] = QChar(static_cast<char16_t>(value + 0x60));
    }
    return converted;
}

bool isPunctuation(char32_t codePoint)
{
    switch (QChar::category(codePoint)) {
    case QChar::Punctuation_Connector:
    case QChar::Punctuation_Dash:
    case QChar::Punctuation_Open:
    case QChar::Punctuation_Close:
    case QChar::Punctuation_InitialQuote:
    case QChar::Punctuation_FinalQuote:
    case QChar::Punctuation_Other:
        return true;
    default:
        return false;
    }
}

} // namespace maru::jp
