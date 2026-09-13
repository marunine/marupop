// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "ocrvariants.h"

#include "core/logging.h"
#include "jp/japanese.h"
#include "lookup/similarkanjitable.h"

#include <algorithm>
#include <array>
#include <optional>
#include <ranges>
#include <string_view>

namespace maru::lookup
{

namespace
{

// The confusion groups in the hiragana key space. つ/づ and ち/ぢ are included although modern
// orthography rarely writes the voiced member, because JMdict keys carry both. う belongs to two
// groups, ゔ and ぅ.
constexpr std::array<std::u16string_view, 33> kKanaGroups = {
    // Voiced and semi-voiced.
    u"かが",
    u"きぎ",
    u"くぐ",
    u"けげ",
    u"こご",
    u"さざ",
    u"しじ",
    u"すず",
    u"せぜ",
    u"そぞ",
    u"ただ",
    u"ちぢ",
    u"つづ",
    u"てで",
    u"とど",
    u"はばぱ",
    u"ひびぴ",
    u"ふぶぷ",
    u"へべぺ",
    u"ほぼぽ",
    u"うゔ",
    // Small and full-size. ゕ and ゖ are the keys of ヵ and ヶ.
    u"あぁ",
    u"いぃ",
    u"うぅ",
    u"えぇ",
    u"おぉ",
    u"つっ",
    u"やゃ",
    u"ゆゅ",
    u"よょ",
    u"わゎ",
    u"かゕ",
    u"けゖ",
};

// Candidate pairs of visually similar source characters from different scripts.
// ヘ/へ and リ/り are absent because they
// share a key already.
constexpr std::array<std::u16string_view, 12> kCrossScriptPairs = {
    u"レし",
    u"ー一",
    u"カ力",
    u"ト上",
    u"ト卜",
    u"ロ口",
    u"ニ二",
    u"ハ八",
    u"ナ十",
    u"エ工",
    u"ホ木",
    u"ル川",
};

// The same deconjugation guard as buildTextInfo(): JL skips a key longer than 35 code units.
constexpr qsizetype maxDeconjugationLength = 35;

// The forms of a variant substituted at position, derived from forms, the forms of the key it was
// substituted in. position is inside that key's untouched prefix, so every form still holds the
// key's character there and no rule read it (deconj::deconjugate()). Empty where a form is too
// short to hold position, which a rule rewriting more than its conjugated ending would cause; the
// caller then deconjugates the variant in full, so the broken bound costs time and no result.
[[nodiscard]] std::optional<std::vector<deconj::Form>>
substitutedForms(const std::vector<deconj::Form> &forms, qsizetype position, QChar substitute, const QString &variant)
{
    std::vector<deconj::Form> out;
    out.reserve(forms.size());
    for (const deconj::Form &form : forms) {
        if (position >= form.text.size()) {
            qCWarning(maru::logLookup) << "Form" << form.text << "is shorter than the untouched prefix of"
                                       << form.originalText << "; deconjugating the variant in full";
            return std::nullopt;
        }
        deconj::Form copy = form;
        copy.text[position] = substitute;
        copy.originalText = variant;
        out.push_back(std::move(copy));
    }
    return out;
}

void appendUnique(QString &out, QChar character)
{
    if (!out.contains(character))
        out.append(character);
}

[[nodiscard]] bool allOf(QStringView text, bool (*test)(char32_t))
{
    return std::ranges::all_of(text, [test](QChar character) {
        return test(character.unicode());
    });
}

} // namespace

QString substitutesFor(QChar keyCharacter)
{
    const char16_t code = keyCharacter.unicode();
    QString out;
    for (const std::u16string_view group : kKanaGroups) {
        if (group.find(code) == std::u16string_view::npos)
            continue;
        for (const char16_t member : group) {
            if (member != code)
                appendUnique(out, QChar(member));
        }
    }
    if (!out.isEmpty())
        return out;

    const auto &index = detail::kSimilarKanjiIndex;
    const auto *found = std::ranges::lower_bound(index, code, {}, &detail::SimilarKanjiEntry::kanji);
    if (found == index.end() || found->kanji != code)
        return {};
    out.reserve(found->count);
    for (std::size_t k = 0; k < found->count; ++k)
        out.append(QChar(detail::kSimilarKanjiNeighbours.at(found->offset + k)));
    return out;
}

QString lookalikesFor(QChar sourceCharacter)
{
    const char16_t code = sourceCharacter.unicode();
    QString out;
    for (const std::u16string_view pair : kCrossScriptPairs) {
        if (pair.front() == code)
            appendUnique(out, QChar(pair.back()));
        else if (pair.back() == code)
            appendUnique(out, QChar(pair.front()));
    }
    return out;
}

QString keySubstitutes(QChar keyCharacter, QChar sourceCharacter)
{
    QString out = substitutesFor(keyCharacter);
    if (sourceCharacter.isNull())
        return out;
    for (const QChar lookalike : lookalikesFor(sourceCharacter)) {
        const QString key = jp::normalizeText(QString(lookalike));
        if (key.size() == 1 && key.front() != keyCharacter)
            appendUnique(out, key.front());
    }
    return out;
}

TextInfo buildVariantTextInfo(const deconj::RuleSet &rules,
                              const TextInfo &exact,
                              qsizetype minTextLength,
                              const VariantOptions &options,
                              QByteArrayView substitutable)
{
    // Shortest eligible candidate first, so that the budget is spent on the short keys, which is
    // where a dictionary word is most likely to be found.
    QList<const Candidate *> eligible;
    for (const Candidate &candidate : exact.candidates) {
        if (candidate.text.size() <= minTextLength)
            continue;
        if (options.maxKeyLength > 0 && candidate.key.size() > options.maxKeyLength)
            continue;
        if (admitsVariants(candidate.text, options))
            eligible.prepend(&candidate);
    }

    QList<QList<Candidate>> groups;
    qsizetype total = 0;
    for (const Candidate *candidate : std::as_const(eligible)) {
        const QString &key = candidate->key;
        const bool aligned = key.size() == candidate->text.size();
        const bool masked = aligned && !substitutable.isEmpty();
        QList<Candidate> group;
        qsizetype keys = 0;
        for (qsizetype i = 0; i < key.size(); ++i) {
            // A surrogate half is never substituted; every table holds BMP characters alone.
            if (key.at(i).isSurrogate())
                continue;
            if (masked && (i >= substitutable.size() || substitutable.at(i) == 0))
                continue;
            const QChar source = aligned ? candidate->text.at(i) : QChar();
            for (const QChar substitute : keySubstitutes(key.at(i), source)) {
                Candidate out;
                out.text = candidate->text;
                out.key = key;
                out.key[i] = substitute;
                std::optional<std::vector<deconj::Form>> derived;
                if (i < candidate->untouchedPrefix)
                    derived = substitutedForms(candidate->forms, i, substitute, out.key);
                if (derived) {
                    out.forms = std::move(*derived);
                    out.untouchedPrefix = candidate->untouchedPrefix;
                } else if (out.key.size() <= maxDeconjugationLength) {
                    out.forms = deconj::deconjugate(rules, out.key, &out.untouchedPrefix);
                } else {
                    out.untouchedPrefix = out.key.size();
                }
                // The elongation runs of the variant rather than of the candidate: レ read for し
                // changes the vowel a following ー lengthens, and ー read for 一 adds a run.
                (void)addLongVowelVariants(rules, out);
                keys += 1 + out.longVowelVariants.size();
                group.append(std::move(out));
            }
        }
        if (options.maxKeys > 0 && total + keys > options.maxKeys)
            break;
        total += keys;
        groups.append(std::move(group));
    }

    // Back to longest first: queryWordDictionary() gives a key to the first candidate that claims
    // it, and the longest match is the one that has to win.
    TextInfo info;
    for (QList<Candidate> &group : groups | std::views::reverse) {
        for (Candidate &candidate : group)
            info.candidates.append(std::move(candidate));
    }
    return info;
}

bool admitsVariants(QStringView text, const VariantOptions &options)
{
    // A single character is never a variant result: one substituted character would then be the
    // whole of the evidence.
    if (text.size() < 2)
        return false;
    if (options.shortAndHiraganaMatches)
        return true;
    if (text.size() == 2 && !allOf(text, jp::isKanji))
        return false;
    return !allOf(text, jp::isHiragana);
}

bool acceptsVariantResult(const Result &result, const QList<FrequencyHit> &frequencies, const VariantOptions &options)
{
    if (!admitsVariants(result.matchedText, options))
        return false;
    if (options.acceptance == VariantAcceptance::AnyWord)
        return true;
    // Katakana on both sides: a lookalike across scripts turns a katakana read into a kanji word
    // (タベース read as 食べる through タ and 夕), and loanword keys are sparse only in katakana.
    if (allOf(result.matchedText, jp::isKatakana) && allOf(result.primarySpelling, jp::isKatakana))
        return true;
    if (result.priorityRank > 0 && result.priorityRank <= variantMaxPriorityRank)
        return true;
    return std::ranges::any_of(frequencies, [](const FrequencyHit &hit) {
        return !hit.higherIsBetter && hit.rank > 0 && hit.rank <= variantMaxFrequencyRank;
    });
}

} // namespace maru::lookup
