// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "ocr/meikicorrections.h"

#include "ocr/meikicorrectiontable.h"

#include <QList>
#include <QString>
#include <QStringView>

#include <algorithm>

namespace maru::ocr
{

namespace
{

// Floor division keeps box partitions consistent for signed coordinates. Coordinates are
// non-negative, so this equals truncation wherever the pipeline calls it.
[[nodiscard]] int floorDiv(int numerator, int denominator)
{
    const int quotient = numerator / denominator;
    return (numerator % denominator != 0 && ((numerator < 0) != (denominator < 0))) ? quotient - 1 : quotient;
}

// Match a context token; character is empty past a line edge.
[[nodiscard]] bool tokenMatches(const CorrectionToken &token, std::optional<QChar> character)
{
    if (token.kind == CorrectionToken::Kind::LineEdge)
        return !character.has_value();
    if (!character.has_value())
        return false;
    if (token.kind == CorrectionToken::Kind::Literal)
        return character->unicode() == token.character;
    return correctionClassOf(*character) == token.kind;
}

[[nodiscard]] QStringView view(std::u16string_view text)
{
    return {text.data(), static_cast<qsizetype>(text.size())};
}

// Check context, orientation and confidence for an occurrence of find at index.
[[nodiscard]] bool
matchesAt(const CorrectionRule &rule, QStringView text, const QList<CharBox> &chars, qsizetype index, bool vertical)
{
    if ((rule.mode == CorrectionMode::Horizontal && vertical) || (rule.mode == CorrectionMode::Vertical && !vertical))
        return false;
    const qsizetype end = index + static_cast<qsizetype>(rule.find.size());
    const auto at = [&text](qsizetype position) -> std::optional<QChar> {
        if (position < 0 || position >= text.size())
            return std::nullopt;
        return text.at(position);
    };
    for (int k = 0; k < rule.beforeCount; ++k) {
        // before is stored nearest last, so the k-th nearest is counted from the back.
        if (!tokenMatches(rule.before.at(rule.beforeCount - 1 - k), at(index - 1 - k)))
            return false;
    }
    for (int k = 0; k < rule.afterCount; ++k) {
        if (!tokenMatches(rule.after.at(k), at(end + k)))
            return false;
    }
    if (!rule.maxConfidence.has_value())
        return true;
    // The comparison runs in double, as Python compares a float32 confidence against the gate.
    return std::ranges::none_of(chars.begin() + index, chars.begin() + end, [&rule](const CharBox &box) {
        return static_cast<double>(box.confidence) > *rule.maxConfidence;
    });
}

// Replaces the span [index, index + find.size()) of chars with replace.
void replaceSpan(QList<CharBox> &out,
                 const QList<CharBox> &chars,
                 qsizetype index,
                 qsizetype length,
                 std::u16string_view replace,
                 bool vertical)
{
    const auto span = chars.mid(index, length);
    float confidence = span.first().confidence;
    for (const CharBox &box : span)
        confidence = std::min(confidence, box.confidence);

    const auto m = static_cast<qsizetype>(replace.size());
    if (m == length) {
        for (qsizetype k = 0; k < m; ++k) {
            CharBox box = span.at(k);
            box.codePoint = replace[k];
            box.confidence = confidence;
            out.append(box);
        }
        return;
    }
    if (m == 0)
        return;

    // The half-open bounding box is [x0, y0, x1, y1] with x1 and y1 one past the last pixel, which is QRect's
    // x() + width() and y() + height().
    int x0 = span.first().box.x();
    int y0 = span.first().box.y();
    int x1 = x0 + span.first().box.width();
    int y1 = y0 + span.first().box.height();
    for (const CharBox &box : span) {
        x0 = std::min(x0, box.box.x());
        y0 = std::min(y0, box.box.y());
        x1 = std::max(x1, box.box.x() + box.box.width());
        y1 = std::max(y1, box.box.y() + box.box.height());
    }
    const int parts = static_cast<int>(m);
    for (int k = 0; k < parts; ++k) {
        QRect rect;
        if (vertical) {
            const int top = y0 + floorDiv((y1 - y0) * k, parts);
            const int bottom = y0 + floorDiv((y1 - y0) * (k + 1), parts);
            rect = QRect(x0, top, x1 - x0, bottom - top);
        } else {
            const int left = x0 + floorDiv((x1 - x0) * k, parts);
            const int right = x0 + floorDiv((x1 - x0) * (k + 1), parts);
            rect = QRect(left, y0, right - left, y1 - y0);
        }
        out.append(CharBox{.codePoint = replace[k], .box = rect, .confidence = confidence});
    }
}

} // namespace

CorrectionToken::Kind correctionClassOf(QChar character)
{
    const char16_t code = character.unicode();
    if (code >= 0x3041 && code <= 0x309F)
        return CorrectionToken::Kind::Hiragana;
    if ((code >= 0x30A1 && code <= 0x30FF) || (code >= 0x31F0 && code <= 0x31FF) || (code >= 0xFF66 && code <= 0xFF9F))
        return CorrectionToken::Kind::Katakana;
    if ((code >= 0x4E00 && code <= 0x9FFF) || (code >= 0x3400 && code <= 0x4DBF) ||
        (code >= 0xF900 && code <= 0xFAFF) || code == u'々' || code == u'〆' || code == u'〇')
        return CorrectionToken::Kind::Kanji;
    // Python's str.isdigit() accepts Numeric_Type Decimal and Digit, which is the set Qt reports
    // a digit value for.
    if (character.digitValue() >= 0)
        return CorrectionToken::Kind::Digit;
    if ((code < 0x80 && character.isLetter()) || (code >= 0xFF21 && code <= 0xFF3A) ||
        (code >= 0xFF41 && code <= 0xFF5A))
        return CorrectionToken::Kind::Latin;
    if (character.isSpace())
        return CorrectionToken::Kind::Space;
    return CorrectionToken::Kind::Other;
}

std::span<const CorrectionRule> meikiCorrectionRules()
{
    return kMeikiCorrectionRules;
}

int applyCorrections(TextLine &line, std::span<const CorrectionRule> rules)
{
    int fired = 0;
    for (const CorrectionRule &rule : rules) {
        const QStringView find = view(rule.find);
        qsizetype index = line.text.indexOf(find);
        if (index < 0)
            continue;

        QList<qsizetype> starts;
        while (index >= 0) {
            if (matchesAt(rule, line.text, line.chars, index, line.vertical)) {
                starts.append(index);
                index = line.text.indexOf(find, index + find.size());
            } else {
                index = line.text.indexOf(find, index + 1);
            }
        }
        if (starts.isEmpty())
            continue;
        ++fired;

        QList<CharBox> out;
        out.reserve(line.chars.size());
        qsizetype previous = 0;
        for (const qsizetype start : std::as_const(starts)) {
            out.append(line.chars.mid(previous, start - previous));
            replaceSpan(out, line.chars, start, find.size(), rule.replace, line.vertical);
            previous = start + find.size();
        }
        out.append(line.chars.mid(previous));

        line.chars = std::move(out);
        line.text.clear();
        line.text.reserve(line.chars.size());
        for (const CharBox &box : std::as_const(line.chars))
            line.text.append(QChar(static_cast<char16_t>(box.codePoint)));
    }

    if (fired > 0) {
        QRect box;
        float confidenceSum = 0.0F;
        for (const CharBox &character : std::as_const(line.chars)) {
            box = box.united(character.box);
            confidenceSum += character.confidence;
        }
        line.box = box;
        line.confidence = line.chars.isEmpty() ? 0.0F : confidenceSum / static_cast<float>(line.chars.size());
    }
    return fired;
}

int applyMeikiCorrections(TextLine &line)
{
    return applyCorrections(line, meikiCorrectionRules());
}

} // namespace maru::ocr
