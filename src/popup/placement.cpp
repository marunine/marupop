// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "popup/placement.h"

#include <QtGlobal>

#include <cmath>

namespace maru::popup
{

namespace
{

// Pushes a coordinate back inside [lowEdge, highEdge - span]. A span wider than the distance
// between the two edges returns lowEdge, which anchors the card at the screen's edge.
int clampToRange(qreal value, int lowEdge, int highEdge, int span)
{
    const int limit = highEdge - span;
    if (limit <= lowEdge) {
        return lowEdge;
    }
    return qBound(lowEdge, static_cast<int>(std::lround(value)), limit);
}

} // namespace

QRect placePopup(QPoint cursor, QSize popup, QRect screen, PopupPositionMode mode, int offset)
{
    const int left = screen.x();
    const int top = screen.y();
    const int right = screen.x() + screen.width();
    const int bottom = screen.y() + screen.height();
    const int width = popup.width();
    const int height = popup.height();

    qreal x = cursor.x() + offset;
    qreal y = cursor.y() + offset;

    switch (mode) {
    case PopupPositionMode::VisualNovel: {
        const int cursorY = cursor.y() - top;
        const int screenHeight = screen.height();
        bool below = true;
        if (cursorY > (2 * screenHeight) / 3) {
            below = false;
        } else if (cursorY < screenHeight / 3) {
            below = true;
        } else {
            below = cursorY < screenHeight / 2;
        }
        y = below ? cursor.y() + offset : cursor.y() - height - offset;

        const qreal half = screen.width() / 2.0;
        const qreal cursorX = cursor.x() - left;
        const qreal anchorRight = cursor.x() + offset;
        const qreal anchorCenter = cursor.x() - (width / 2.0);
        const qreal anchorLeft = cursor.x() - width - offset;
        if (half <= 0.0) {
            x = anchorCenter;
        } else if (cursorX < half) {
            const qreal ratio = cursorX / half;
            x = (anchorRight * (1.0 - ratio)) + (anchorCenter * ratio);
        } else {
            const qreal ratio = qBound(0.0, (cursorX - half) / half, 1.0);
            x = (anchorCenter * (1.0 - ratio)) + (anchorLeft * ratio);
        }
        break;
    }
    case PopupPositionMode::FlipHorizontally:
        if (cursor.x() + offset + width > right) {
            x = cursor.x() - width - offset;
        }
        break;
    case PopupPositionMode::FlipVertically:
        if (cursor.y() + offset + height > bottom) {
            y = cursor.y() - height - offset;
        }
        break;
    case PopupPositionMode::FlipBoth:
        if (cursor.x() + offset + width > right) {
            x = cursor.x() - width - offset;
        }
        if (cursor.y() + offset + height > bottom) {
            y = cursor.y() - height - offset;
        }
        break;
    }

    return QRect{QPoint{clampToRange(x, left, right, width), clampToRange(y, top, bottom, height)}, popup};
}

QRect placePopupAvoiding(QPoint cursor, QSize popup, QRect screen, PopupPositionMode mode, int offset, QRect avoid)
{
    const QRect unconstrained = placePopup(cursor, popup, screen, mode, offset);
    if (avoid.isEmpty() || !unconstrained.intersects(avoid)) {
        return unconstrained;
    }
    const QRect blocked = avoid.intersected(screen);
    if (blocked.isEmpty()) {
        return unconstrained;
    }

    // The four bands of screen that hold no part of blocked, in the tie-breaking order.
    const QRect bands[] = {
        QRect{screen.left(), screen.top(), screen.width(), blocked.top() - screen.top()},
        QRect{screen.left(), blocked.bottom() + 1, screen.width(), screen.bottom() - blocked.bottom()},
        QRect{screen.left(), screen.top(), blocked.left() - screen.left(), screen.height()},
        QRect{blocked.right() + 1, screen.top(), screen.right() - blocked.right(), screen.height()},
    };

    QRect best;
    qint64 bestDistance = 0;
    for (const QRect &band : bands) {
        if (band.width() < popup.width() || band.height() < popup.height()) {
            continue;
        }
        // The unconstrained placement pushed into the band, which is the position inside the band
        // closest to where the mode wanted the card.
        const QRect candidate{QPoint{clampToRange(unconstrained.x(), band.left(), band.right() + 1, popup.width()),
                                     clampToRange(unconstrained.y(), band.top(), band.bottom() + 1, popup.height())},
                              popup};
        const QPoint delta = candidate.center() - unconstrained.center();
        const auto distance = static_cast<qint64>(delta.x()) * delta.x() + static_cast<qint64>(delta.y()) * delta.y();
        if (best.isEmpty() || distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best.isEmpty() ? unconstrained : best;
}

} // namespace maru::popup
