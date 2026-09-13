// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "capture/scanregion.h"

#include "capture/framesource.h"

#include <QGuiApplication>
#include <QScreen>

#include <algorithm>
#include <cmath>

namespace maru::capture
{

namespace
{

// The rect moved by the smallest offset that puts it inside bounds. rect must not be larger
// than bounds in either dimension; a caller that cannot guarantee that clamps the size first.
QRect movedInto(QRect rect, QRect bounds)
{
    if (rect.left() < bounds.left()) {
        rect.moveLeft(bounds.left());
    } else if (rect.right() > bounds.right()) {
        rect.moveRight(bounds.right());
    }
    if (rect.top() < bounds.top()) {
        rect.moveTop(bounds.top());
    } else if (rect.bottom() > bounds.bottom()) {
        rect.moveBottom(bounds.bottom());
    }
    return rect;
}

// The rect moved by the smallest offset that puts point inside it. The offset is at most the
// distance from point to the nearer edge, so a rect already inside bounds that is moved to
// reach a point inside bounds stays inside bounds.
QRect movedToContain(QRect rect, QPoint point)
{
    if (point.x() < rect.left()) {
        rect.moveLeft(point.x());
    } else if (point.x() > rect.right()) {
        rect.moveRight(point.x());
    }
    if (point.y() < rect.top()) {
        rect.moveTop(point.y());
    } else if (point.y() > rect.bottom()) {
        rect.moveBottom(point.y());
    }
    return rect;
}

QSize clampedToBounds(QSize size, QRect bounds)
{
    return {std::min(size.width(), bounds.width()), std::min(size.height(), bounds.height())};
}

QPoint clampedToBounds(QPoint point, QRect bounds)
{
    return {std::clamp(point.x(), bounds.left(), bounds.right()), std::clamp(point.y(), bounds.top(), bounds.bottom())};
}

} // namespace

QRect workspaceRect()
{
    QRect rect;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (const QScreen *screen : screens) {
        rect |= screen->geometry();
    }
    return rect;
}

QRect initialRect(QPoint cursor, QSize size, QRect workspace)
{
    if (size.isEmpty() || workspace.isEmpty()) {
        return {};
    }
    QRect rect{QPoint{0, 0}, clampedToBounds(size, workspace)};
    rect.moveCenter(clampedToBounds(cursor, workspace));
    return movedInto(rect, workspace);
}

std::optional<QRect> grow(QRect current, QPoint cursor, QSize maxSize, QRect workspace)
{
    if (workspace.isEmpty() || maxSize.isEmpty()) {
        return std::nullopt;
    }
    const QSize limit = clampedToBounds(maxSize, workspace);
    if (current.width() >= limit.width() && current.height() >= limit.height()) {
        return std::nullopt;
    }
    if (current.isEmpty()) {
        return initialRect(cursor, limit, workspace);
    }
    const QSize next{std::min(current.width() * 2, limit.width()), std::min(current.height() * 2, limit.height())};

    QRect grown{QPoint{0, 0}, next};
    // About the centre of current, so the text already recognized inside current stays inside
    // the grown rect where the pointer has not moved.
    grown.moveCenter(current.center());
    grown = movedInto(grown, workspace);
    return movedToContain(grown, clampedToBounds(cursor, workspace));
}

QRect wlrTileFor(QRect requested, QRect output)
{
    if (requested.isEmpty() || output.isEmpty()) {
        return {};
    }
    const QSize size = clampedToBounds(requested.size(), output);
    // Half the tile, so the centre of requested lands at most a quarter of the tile from the
    // tile's centre and therefore at least a quarter of it from the tile's edge.
    const int strideX = std::max(1, size.width() / 2);
    const int strideY = std::max(1, size.height() / 2);

    // The origin that would centre the tile on requested, expressed on the lattice anchored at
    // output.topLeft(), rounded to the nearest lattice point.
    const QPoint centre = requested.center();
    const int wantX = centre.x() - size.width() / 2 - output.left();
    const int wantY = centre.y() - size.height() / 2 - output.top();
    const int indexX = static_cast<int>(std::lround(static_cast<double>(wantX) / strideX));
    const int indexY = static_cast<int>(std::lround(static_cast<double>(wantY) / strideY));

    QRect tile{output.left() + indexX * strideX, output.top() + indexY * strideY, size.width(), size.height()};
    return movedInto(tile, output);
}

bool touchesEdge(QRect lineBox, QRect frame, int margin)
{
    if (lineBox.isEmpty() || frame.isEmpty()) {
        return false;
    }
    return lineBox.left() - frame.left() <= margin || frame.right() - lineBox.right() <= margin ||
           lineBox.top() - frame.top() <= margin || frame.bottom() - lineBox.bottom() <= margin;
}

QPoint imageToLogical(const Frame &frame, QPoint imagePoint)
{
    const qreal scale = frame.scale > 0 ? frame.scale : 1.0;
    return frame.logicalRect.topLeft() + QPoint{qRound(imagePoint.x() / scale), qRound(imagePoint.y() / scale)};
}

QRect imageToLogical(const Frame &frame, QRect imageRect)
{
    const qreal scale = frame.scale > 0 ? frame.scale : 1.0;
    const QPoint topLeft = imageToLogical(frame, imageRect.topLeft());
    return {topLeft,
            QSize{std::max(1, qRound(imageRect.width() / scale)), std::max(1, qRound(imageRect.height() / scale))}};
}

QPoint logicalToImage(const Frame &frame, QPoint logicalPoint)
{
    const qreal scale = frame.scale > 0 ? frame.scale : 1.0;
    const QPoint local = logicalPoint - frame.logicalRect.topLeft();
    return {qRound(local.x() * scale), qRound(local.y() * scale)};
}

QRect logicalToImage(const Frame &frame, QRect logicalRect)
{
    const qreal scale = frame.scale > 0 ? frame.scale : 1.0;
    const QPoint topLeft = logicalToImage(frame, logicalRect.topLeft());
    return {topLeft,
            QSize{std::max(1, qRound(logicalRect.width() * scale)), std::max(1, qRound(logicalRect.height() * scale))}};
}

} // namespace maru::capture
