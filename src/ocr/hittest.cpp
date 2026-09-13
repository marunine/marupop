// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "ocr/hittest.h"

#include <algorithm>
#include <cmath>

namespace maru::ocr
{

namespace
{

// The half-open range of character indices of the line holding charIndex. A paragraph with an
// empty lineStarts is treated as one line, which is what a single-line paragraph produces.
std::pair<int, int> lineRange(const Paragraph &paragraph, int charIndex)
{
    int start = 0;
    int end = static_cast<int>(paragraph.chars.size());
    for (const int lineStart : paragraph.lineStarts) {
        if (lineStart > charIndex) {
            end = lineStart;
            break;
        }
        start = lineStart;
    }
    return {start, end};
}

// Distance from point to the closest point of box, in pixels. A point inside box gives 0.
double distanceTo(QRect box, QPoint point)
{
    const double dx = std::max({box.left() - point.x(), 0, point.x() - box.right()});
    const double dy = std::max({box.top() - point.y(), 0, point.y() - box.bottom()});
    return std::hypot(dx, dy);
}

} // namespace

QRect extendedCharBox(const Paragraph &paragraph, int charIndex)
{
    if (charIndex < 0 || charIndex >= paragraph.chars.size()) {
        return {};
    }
    const QRect box = paragraph.chars.at(charIndex).box;
    const auto [start, end] = lineRange(paragraph, charIndex);
    const bool hasPrevious = charIndex > start;
    const bool hasNext = charIndex + 1 < end;
    int left = box.left();
    int top = box.top();
    int right = box.right();
    int bottom = box.bottom();

    if (paragraph.vertical) {
        if (hasPrevious) {
            top = std::min(top, paragraph.chars.at(charIndex - 1).box.bottom() + 1);
        }
        if (hasNext) {
            bottom = std::max(bottom, paragraph.chars.at(charIndex + 1).box.top() - 1);
        }
    } else {
        if (hasPrevious) {
            left = std::min(left, paragraph.chars.at(charIndex - 1).box.right() + 1);
        }
        if (hasNext) {
            right = std::max(right, paragraph.chars.at(charIndex + 1).box.left() - 1);
        }
    }
    return QRect{QPoint{left, top}, QPoint{right, bottom}};
}

QRect charSpanRect(const Paragraph &paragraph, int from, int count)
{
    QRect span;
    const int first = std::max(from, 0);
    const int last = std::min(from + count, static_cast<int>(paragraph.chars.size()));
    for (int index = first; index < last; ++index) {
        span = span.united(paragraph.chars.at(index).box);
    }
    return span;
}

std::optional<Hit> hitTest(const Result &result, QPoint imagePoint)
{
    for (qsizetype paragraphIndex = 0; paragraphIndex < result.paragraphs.size(); ++paragraphIndex) {
        const Paragraph &paragraph = result.paragraphs.at(paragraphIndex);
        if (!paragraph.box.contains(imagePoint)) {
            continue;
        }
        for (qsizetype charIndex = 0; charIndex < paragraph.chars.size(); ++charIndex) {
            if (extendedCharBox(paragraph, static_cast<int>(charIndex)).contains(imagePoint)) {
                return Hit{.paragraph = static_cast<int>(paragraphIndex), .charIndex = static_cast<int>(charIndex)};
            }
        }
    }

    // Nothing held the point: the nearest character box within kHitTolerancePx wins, which
    // covers a pointer resting in the gap between two lines and a character box the model
    // reported one pixel wide.
    std::optional<Hit> nearest;
    double nearestDistance = 0.0;
    for (qsizetype paragraphIndex = 0; paragraphIndex < result.paragraphs.size(); ++paragraphIndex) {
        const Paragraph &paragraph = result.paragraphs.at(paragraphIndex);
        for (qsizetype charIndex = 0; charIndex < paragraph.chars.size(); ++charIndex) {
            const double distance = distanceTo(paragraph.chars.at(charIndex).box, imagePoint);
            if (distance > static_cast<double>(kHitTolerancePx)) {
                continue;
            }
            // The first character at a given distance wins, which keeps reading order where
            // the pointer sits exactly between two boxes.
            if (!nearest.has_value() || distance < nearestDistance) {
                nearestDistance = distance;
                nearest = Hit{.paragraph = static_cast<int>(paragraphIndex), .charIndex = static_cast<int>(charIndex)};
            }
        }
    }
    return nearest;
}

} // namespace maru::ocr
