// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The pointer-to-character mapping over one recognition result, in source-image pixels. The
// rule is meikipop's box extension: a character box is stretched along the reading axis until
// it meets its neighbours, so the gaps between glyphs belong to a character rather than to
// nothing. The implementation is original to MaruPop; see NOTICE for attribution.
#pragma once

#include "ocr/ocrtypes.h"

#include <QPoint>
#include <QRect>

#include <optional>

namespace maru::ocr
{

// The radius searched around the pointer once box containment has failed, in source-image
// pixels. A capture is grabbed at native resolution, so the value is a pixel count on the
// screen the pointer is on.
inline constexpr int kHitTolerancePx = 6;

// One character of one paragraph. Both indices index Result::paragraphs and Paragraph::chars,
// and charIndex indexes Paragraph::text at the same position.
struct Hit
{
    int paragraph = -1;
    int charIndex = -1;
};

// The character under imagePoint. Paragraphs whose box holds the point are searched first,
// with each character box extended to its neighbours along the reading axis; a point that no
// extended box holds falls back to the nearest character box within kHitTolerancePx.
[[nodiscard]] std::optional<Hit> hitTest(const Result &result, QPoint imagePoint);

// The character box extended to its neighbours along the reading axis, which is the rectangle
// hitTest() tests against. Exposed for the tests and for highlight geometry.
[[nodiscard]] QRect extendedCharBox(const Paragraph &paragraph, int charIndex);

// The union of count character boxes starting at from, for the popup highlight. An empty
// range, or a range outside the paragraph, gives a null rectangle. A span crossing a line
// break covers both lines and everything between them.
[[nodiscard]] QRect charSpanRect(const Paragraph &paragraph, int from, int count);

} // namespace maru::ocr
