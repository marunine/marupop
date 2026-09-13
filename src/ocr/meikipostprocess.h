// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The meikiocr output mapping, ported from meikiocr/ocr.py (Apache-2.0,
// https://github.com/rtr46/meikiocr), functions _postprocess_detection_results,
// _postprocess_recognition_results and _fix_swapped_pairs. Every truncation point and every
// constant divisor follows the pinned upstream implementation (see NOTICE). Changing
// rounding order can move character boxes by one pixel or more.
#pragma once

#include "ocr/ocrtypes.h"

#include <QList>
#include <QPair>
#include <QRect>
#include <QSize>
#include <QString>

#include <optional>

namespace maru::ocr
{

// Interval-NMS threshold on the reading axis, for both orientations.
inline constexpr float kOverlapThreshold = 0.3F;
// Guard added to every interval length before the division, matching meikiocr's EPSILON.
inline constexpr double kIntervalEpsilon = 1e-6;

// One character candidate before the interval NMS. interval is the extent along the reading
// axis: x for a horizontal line, y for a vertical line.
struct Candidate
{
    char32_t codePoint = 0;
    QRect box;
    float confidence = 0.0F;
    int intervalStart = 0;
    int intervalEnd = 0;
};

// Detection boxes above threshold, clamped to sourceSize, truncated to integers and sorted by
// their top edge. boxes holds count sets of 4 floats in xyxy order, already in source-image
// pixel coordinates because the graph scales them by orig_target_sizes.
[[nodiscard]] QList<QRect>
detectionBoxes(const float *boxes, const float *scores, int count, QSize sourceSize, float threshold);

// One raw recognition box mapped back into source-image coordinates. rawBox holds 4 floats in
// xyxy order, in the coordinate space of the model input. cropBox is the crop or segment the
// row was produced from. Returns nullopt where the character lies entirely in the padding, and
// for a vertical character whose mapped height is 0.
[[nodiscard]] std::optional<Candidate> mapCharacter(char32_t codePoint,
                                                    const float *rawBox,
                                                    float score,
                                                    QRect cropBox,
                                                    int effectiveWidth,
                                                    int effectiveHeight,
                                                    bool vertical);

// True for the seven Unicode punctuation categories Pc, Pd, Ps, Pe, Pi, Pf and Po, which is
// what unicodedata.category(c).startswith("P") selects. U+30FC KATAKANA-HIRAGANA PROLONGED
// SOUND MARK is Lm and is therefore not punctuation.
[[nodiscard]] bool isPunctuation(char32_t codePoint);

// Multiplies the confidence of every punctuation candidate by factor, which lets an
// overlapping non-punctuation candidate win the NMS. meikipop passes 0.2; the meikiocr library
// default is 1.0, which this function treats as a no-op.
void applyPunctuationFactor(QList<Candidate> &candidates, float factor);

// One-dimensional NMS over the reading axis. Candidates are taken in descending confidence and
// accepted where their overlap with every accepted interval stays at or below threshold; the
// denominator is the shorter of the two intervals, not their union. The accepted candidates
// come back sorted by intervalStart, which is reading order.
[[nodiscard]] QList<Candidate> intervalNms(QList<Candidate> candidates, float threshold);

// The eight character pairs the recognition model emits in reverse order, in the insertion
// order of meikiocr's SWAPPED_PAIRS dict, which is the order the pairs are tried in.
[[nodiscard]] const QList<QPair<QString, QString>> &swappedPairs();

// Swaps the code points of every occurrence of each pair in text and in chars. The boxes stay
// where they are, because only the character identities were exchanged. Search
// continues after each corrected pair so repeated occurrences are all corrected.
void fixSwappedPairs(QString &text, QList<CharBox> &chars);

// The full per-box pipeline: punctuation factor, interval NMS, reading order, text assembly
// and the swapped-pair fix. A candidate outside the Basic Multilingual Plane is dropped, which
// keeps the text.size() == chars.size() invariant of ocr::TextLine.
[[nodiscard]] TextLine
buildTextLine(QList<Candidate> candidates, bool vertical, float punctuationFactor, float overlapThreshold);

} // namespace maru::ocr
