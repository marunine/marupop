// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "textinfo.h"

#include "jp/japanese.h"

#include <QSet>

namespace maru::lookup
{

namespace
{

// The deconjugator is skipped for a key longer than this, which is JL's guard against the
// exponential end of the search on a sentence-length span.
constexpr qsizetype maxDeconjugationLength = 35;

// Elongation variants are built only for a key at most this long: the variant count is 2^runs,
// and each variant is deconjugated in full.
constexpr qsizetype maxLongVowelVariantLength = 20;

// The count above which JL builds no variants at all. jp::countNonConsecutiveLongVowelMarks()
// caps its answer at 4, so this is "four or more runs".
constexpr int maxLongVowelRuns = 4;

// JL's s_longVowelMarkCharsNotNormalized (JapaneseUtils.cs). It is the elongation set of
// jp/japanesetables.h plus U+FF5E FULLWIDTH TILDE, which NFKC folds to U+301C but which is
// still tested here against the raw input.
[[nodiscard]] bool isRawLongVowelMark(QChar character)
{
    const char16_t value = character.unicode();
    return value == u'ー' || value == u'〜' || value == u'~' || value == u'～';
}

} // namespace

int addLongVowelVariants(const deconj::RuleSet &rules, Candidate &candidate)
{
    // A text that itself starts on an elongation mark carries no run to resolve: the mark has no
    // preceding kana to lengthen.
    if (candidate.text.isEmpty() || isRawLongVowelMark(candidate.text.at(0)) ||
        candidate.key.size() > maxLongVowelVariantLength)
        return -1;

    const int runs = jp::countNonConsecutiveLongVowelMarks(candidate.key);
    if (runs > 0 && runs < maxLongVowelRuns) {
        candidate.longVowelVariants = jp::normalizeLongVowelMark(candidate.key);
        candidate.longVowelVariantForms.reserve(candidate.longVowelVariants.size());
        for (const QString &variant : std::as_const(candidate.longVowelVariants))
            candidate.longVowelVariantForms.push_back(deconj::deconjugate(rules, variant));
    }
    return runs;
}

TextInfo buildTextInfo(const deconj::RuleSet &rules, QStringView textFromCursor)
{
    TextInfo info;
    if (textFromCursor.isEmpty())
        return info;

    const qsizetype length = textFromCursor.size();
    info.candidates.reserve(length);

    bool countLongVowelMarks = true;

    QSet<QString> seenLemmas;
    for (qsizetype i = 0; i < length; ++i) {
        // Shortening by one code unit at a time would cut a surrogate pair in half, so the
        // truncation that would end on a high surrogate is skipped rather than clamped: the next
        // iteration drops the whole pair.
        if (textFromCursor.at(length - i - 1).isHighSurrogate())
            continue;

        Candidate candidate;
        candidate.text = textFromCursor.left(length - i).toString();
        candidate.key = jp::normalizeText(candidate.text);

        const bool loneFuseji =
            i == length - 1 && !candidate.key.isEmpty() && candidate.key.at(0) == jp::kNormalizedFuseji;
        if (candidate.key.size() <= maxDeconjugationLength && !loneFuseji)
            candidate.forms = deconj::deconjugate(rules, candidate.key, &candidate.untouchedPrefix);
        else
            candidate.untouchedPrefix = candidate.key.size();

        // Every shorter prefix is a prefix of this one, so once a candidate holds no run no
        // shorter candidate can either. JL stops counting here rather than paying for the scan on
        // every remaining prefix.
        if (countLongVowelMarks && addLongVowelVariants(rules, candidate) == 0)
            countLongVowelMarks = false;

        for (const deconj::Form &form : candidate.forms) {
            if (!seenLemmas.contains(form.text)) {
                seenLemmas.insert(form.text);
                info.deconjugatedTexts.append(form.text);
            }
        }
        for (const std::vector<deconj::Form> &forms : candidate.longVowelVariantForms) {
            for (const deconj::Form &form : forms) {
                if (!seenLemmas.contains(form.text)) {
                    seenLemmas.insert(form.text);
                    info.deconjugatedTexts.append(form.text);
                }
            }
        }

        info.candidates.append(std::move(candidate));
    }

    return info;
}

} // namespace maru::lookup
