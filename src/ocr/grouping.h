// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Recognized lines grouped into the reading units a dictionary lookup runs over. The rules are
// based on meikipop's observable grouping behavior; see NOTICE for attribution.
// The grouping implementation is original to MaruPop.
//
// The rules apply to both backends: MeikiOcrBackend and ScreenAiBackend each report lines with
// per-character boxes, and ocr::OcrService calls groupLines() on whichever one produced them.
#pragma once

#include "ocr/ocrtypes.h"

#include <QList>
#include <QRect>
#include <QSize>
#include <QStringView>

namespace maru::ocr
{

// A line counts as furigana where its cross-axis extent falls below this fraction of the
// median extent of the lines sharing its orientation.
inline constexpr double kFuriganaSizeRatio = 0.65;
// A line counts as vertical where its height exceeds its width by this factor. The rule is
// stricter than the h > w rule meikiocr uses to pick a recognition model, so a line the
// recognition pass treated as vertical can still group as horizontal.
inline constexpr double kVerticalAspectRatio = 1.5;
// Two lines are adjacent where they overlap on the cross axis by more than this fraction of
// the shorter of the two extents.
inline constexpr double kAdjacencyOverlapRatio = 0.5;
// Two lines are adjacent where their centre distance along the reading-order axis stays below
// this multiple of the larger of the two cross-axis extents.
inline constexpr double kAdjacencyCentreRatio = 1.9;

// Lines grouped into paragraphs: vertical paragraphs first, then horizontal paragraphs, then
// one paragraph per furigana line. Lines holding no hiragana, katakana or CJK ideograph are
// dropped, which is what removes the Latin-only lines a screenshot of a game UI produces.
// imageSize bounds every reported box; an invalid size leaves the boxes unbounded.
[[nodiscard]] QList<Paragraph> groupLines(const QList<TextLine> &lines, QSize imageSize);

// True where text holds at least one hiragana, katakana or kanji code point, tested through
// jp::isHiragana(), jp::isKatakana() and jp::isKanji(). The three cover the ranges meikipop
// tests (U+3040..U+30FF and U+4E00..U+9FAF) and further CJK blocks; fullwidth Latin, General
// Punctuation and the geometric shapes jp::isJapanese() also accepts are excluded, because a
// line holding those alone carries nothing to look up.
[[nodiscard]] bool containsJapanese(QStringView text);

// True where two boxes of the given orientation belong in one paragraph.
[[nodiscard]] bool linesAdjacent(QRect first, QRect second, bool vertical);

// The median of values, as Python's statistics.median computes it: the mean of the two middle
// values for an even count. An empty list gives 0.0.
[[nodiscard]] double median(QList<double> values);

} // namespace maru::ocr
