// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "ocr/meikipostprocess.h"

#include "core/logging.h"
#include "ocr/meikipreprocess.h"

#include <QChar>
#include <QLoggingCategory>

#include <algorithm>

namespace maru::ocr
{

namespace
{

// A half-open [x1,y1,x2,y2] box as a QRect. QRect::right() is inclusive, so the exclusive
// edges are never assigned through setRight()/setBottom().
QRect rectFromEdges(int x1, int y1, int x2, int y2)
{
    return QRect{x1, y1, x2 - x1, y2 - y1};
}

} // namespace

QList<QRect> detectionBoxes(const float *boxes, const float *scores, int count, QSize sourceSize, float threshold)
{
    QList<QRect> result;
    if (boxes == nullptr || scores == nullptr) {
        return result;
    }
    const auto maxX = static_cast<float>(sourceSize.width());
    const auto maxY = static_cast<float>(sourceSize.height());
    for (int index = 0; index < count; ++index) {
        if (scores[index] <= threshold) {
            continue;
        }
        const float *box = boxes + (static_cast<ptrdiff_t>(index) * 4);
        const int x1 = static_cast<int>(std::clamp(box[0], 0.0F, maxX));
        const int y1 = static_cast<int>(std::clamp(box[1], 0.0F, maxY));
        const int x2 = static_cast<int>(std::clamp(box[2], 0.0F, maxX));
        const int y2 = static_cast<int>(std::clamp(box[3], 0.0F, maxY));
        result.append(rectFromEdges(x1, y1, x2, y2));
    }
    // Top to bottom, stable, so boxes sharing a top edge keep the order the head emitted them
    // in. The recognition results are written back by detection index, so this sort fixes the
    // reading order of the whole result.
    std::ranges::stable_sort(result, [](const QRect &left, const QRect &right) {
        return left.y() < right.y();
    });
    return result;
}

std::optional<Candidate> mapCharacter(char32_t codePoint,
                                      const float *rawBox,
                                      float score,
                                      QRect cropBox,
                                      int effectiveWidth,
                                      int effectiveHeight,
                                      bool vertical)
{
    if (rawBox == nullptr || cropBox.width() <= 0 || cropBox.height() <= 0) {
        return std::nullopt;
    }
    const auto cropWidth = static_cast<float>(cropBox.width());
    const auto cropHeight = static_cast<float>(cropBox.height());
    const int globalX = cropBox.x();
    const int globalY = cropBox.y();
    float rx1 = rawBox[0];
    float ry1 = rawBox[1];
    float rx2 = rawBox[2];
    float ry2 = rawBox[3];

    Candidate candidate;
    candidate.codePoint = codePoint;
    candidate.confidence = score;

    if (!vertical) {
        if (effectiveWidth <= 0 || rx1 >= static_cast<float>(effectiveWidth)) {
            return std::nullopt;
        }
        rx1 = std::min(rx1, static_cast<float>(effectiveWidth));
        rx2 = std::min(rx2, static_cast<float>(effectiveWidth));
        // The scan axis divides by effectiveWidth, the cross axis by the constant model input
        // height of 32 px. The asymmetry is meikiocr's and is load-bearing.
        const float cx1 = (rx1 / static_cast<float>(effectiveWidth)) * cropWidth;
        const float cx2 = (rx2 / static_cast<float>(effectiveWidth)) * cropWidth;
        const float cy1 = (ry1 / static_cast<float>(kRecognitionHeight)) * cropHeight;
        const float cy2 = (ry2 / static_cast<float>(kRecognitionHeight)) * cropHeight;
        // The truncation happens before the global offset is added, which is why the two steps
        // are never folded into one expression.
        const int x1 = globalX + static_cast<int>(cx1);
        const int y1 = globalY + static_cast<int>(cy1);
        const int x2 = globalX + static_cast<int>(cx2);
        const int y2 = globalY + static_cast<int>(cy2);
        candidate.box = rectFromEdges(x1, y1, x2, y2);
        candidate.intervalStart = x1;
        candidate.intervalEnd = x2;
        return candidate;
    }

    if (effectiveHeight <= 0 || ry1 >= static_cast<float>(effectiveHeight)) {
        return std::nullopt;
    }
    ry1 = std::min(ry1, static_cast<float>(effectiveHeight));
    ry2 = std::min(ry2, static_cast<float>(effectiveHeight));
    // Vertical mirrors the horizontal asymmetry: the cross axis divides by the constant model
    // input width of 32 px, the scan axis by effectiveHeight.
    const float cx1 = (rx1 / static_cast<float>(kVerticalWidth)) * cropWidth;
    const float cx2 = (rx2 / static_cast<float>(kVerticalWidth)) * cropWidth;
    const float cy1 = (ry1 / static_cast<float>(effectiveHeight)) * cropHeight;
    const float cy2 = (ry2 / static_cast<float>(effectiveHeight)) * cropHeight;
    const int x1 = globalX + static_cast<int>(cx1);
    const int y1 = globalY + static_cast<int>(cy1);
    const int x2 = globalX + static_cast<int>(cx2);
    const int y2 = globalY + static_cast<int>(cy2);
    if (y2 <= y1) {
        return std::nullopt;
    }
    candidate.box = rectFromEdges(x1, y1, x2, y2);
    candidate.intervalStart = y1;
    candidate.intervalEnd = y2;
    return candidate;
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

void applyPunctuationFactor(QList<Candidate> &candidates, float factor)
{
    if (factor == 1.0F) {
        return;
    }
    for (Candidate &candidate : candidates) {
        if (isPunctuation(candidate.codePoint)) {
            candidate.confidence *= factor;
        }
    }
}

QList<Candidate> intervalNms(QList<Candidate> candidates, float threshold)
{
    // Descending confidence, stable so equal confidences keep the emission order.
    std::ranges::stable_sort(candidates, [](const Candidate &left, const Candidate &right) {
        return left.confidence > right.confidence;
    });

    QList<Candidate> accepted;
    accepted.reserve(candidates.size());
    for (const Candidate &candidate : std::as_const(candidates)) {
        const double length = (candidate.intervalEnd - candidate.intervalStart) + kIntervalEpsilon;
        bool overlaps = false;
        for (const Candidate &other : std::as_const(accepted)) {
            if (candidate.intervalStart >= other.intervalEnd || other.intervalStart >= candidate.intervalEnd) {
                continue;
            }
            const int start = std::max(candidate.intervalStart, other.intervalStart);
            const int end = std::min(candidate.intervalEnd, other.intervalEnd);
            const double intersection = std::max(0, end - start);
            const double otherLength = (other.intervalEnd - other.intervalStart) + kIntervalEpsilon;
            // The denominator is the shorter interval, so a short candidate inside a long one
            // is suppressed even though the union ratio would be small.
            if (intersection / std::min(length, otherLength) > threshold) {
                overlaps = true;
                break;
            }
        }
        if (!overlaps) {
            accepted.append(candidate);
        }
    }

    std::ranges::stable_sort(accepted, [](const Candidate &left, const Candidate &right) {
        return left.intervalStart < right.intervalStart;
    });
    return accepted;
}

const QList<QPair<QString, QString>> &swappedPairs()
{
    static const QList<QPair<QString, QString>> pairs = {
        {QStringLiteral("儡傀"), QStringLiteral("傀儡")},
        {QStringLiteral("談冗"), QStringLiteral("冗談")},
        {QStringLiteral("汰淘"), QStringLiteral("淘汰")},
        {QStringLiteral("沱滂"), QStringLiteral("滂沱")},
        {QStringLiteral("攣痙"), QStringLiteral("痙攣")},
        {QStringLiteral("酊酩"), QStringLiteral("酩酊")},
        {QStringLiteral("麭麺"), QStringLiteral("麺麭")},
        {QStringLiteral("哭慟"), QStringLiteral("慟哭")},
    };
    return pairs;
}

void fixSwappedPairs(QString &text, QList<CharBox> &chars)
{
    for (const QPair<QString, QString> &pair : swappedPairs()) {
        // Every occurrence of a pair is corrected: the search resumes at the 2 characters
        // after the corrected one, and the corrected text differs from the searched text, so
        // the loop advances by at least 2 characters per iteration.
        for (qsizetype index = text.indexOf(pair.first); index >= 0; index = text.indexOf(pair.first, index + 2)) {
            if (index + 1 >= chars.size()) {
                break;
            }
            text.replace(index, 2, pair.second);
            std::swap(chars[index].codePoint, chars[index + 1].codePoint);
        }
    }
}

TextLine buildTextLine(QList<Candidate> candidates, bool vertical, float punctuationFactor, float overlapThreshold)
{
    TextLine line;
    line.vertical = vertical;
    applyPunctuationFactor(candidates, punctuationFactor);
    const QList<Candidate> accepted = intervalNms(std::move(candidates), overlapThreshold);

    QRect box;
    float confidenceSum = 0.0F;
    for (const Candidate &candidate : accepted) {
        if (candidate.codePoint > 0xFFFF) {
            // ocr::TextLine indexes chars by UTF-16 code unit, so a supplementary-plane
            // character would break the text.size() == chars.size() invariant.
            qCWarning(logOcr) << "dropping a non-BMP code point" << Qt::hex << static_cast<uint>(candidate.codePoint);
            continue;
        }
        line.text.append(QChar(static_cast<char16_t>(candidate.codePoint)));
        line.chars.append(
            CharBox{.codePoint = candidate.codePoint, .box = candidate.box, .confidence = candidate.confidence});
        box = box.united(candidate.box);
        confidenceSum += candidate.confidence;
    }
    fixSwappedPairs(line.text, line.chars);
    line.box = box;
    line.confidence = line.chars.isEmpty() ? 0.0F : confidenceSum / static_cast<float>(line.chars.size());
    return line;
}

} // namespace maru::ocr
