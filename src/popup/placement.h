// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The pure placement math of the popup card against the pointer.
#pragma once

#include "core/enums.h"

#include <QPoint>
#include <QRect>
#include <QSize>

namespace maru::popup
{

// The rectangle a popup of size popup takes next to cursor on the screen whose logical
// geometry is screen, under the given mode, with offset logical pixels between the pointer and
// the card.
//
// The four modes follow meikipop's Popup.move_to behavior (see NOTICE):
//
//   VisualNovel        the card goes below the pointer while the pointer is in the upper third
//                      of the screen and above it while the pointer is in the lower third; in
//                      the middle third the exact half decides. The horizontal anchor
//                      interpolates continuously from "right of the pointer" at the left edge,
//                      through "centered on the pointer" at the half, to "left of the pointer"
//                      at the right edge.
//   FlipHorizontally   the card goes right of the pointer, and flips left when the right side
//                      would leave the screen. The vertical position is pushed back in.
//   FlipVertically     the card goes below the pointer, and flips above when the bottom would
//                      leave the screen. The horizontal position is pushed back in.
//   FlipBoth           both axes flip independently.
//
// Every mode ends with a clamp into screen, so the result is inside screen whenever the popup
// fits in it. A popup larger than the screen is anchored at the screen's top left corner.
//
// The clamp uses the exclusive edges screen.x() + screen.width() and
// screen.y() + screen.height(), one logical pixel past meikipop's QRect.right() and
// QRect.bottom(); the card therefore reaches the true screen edge where meikipop stops one
// pixel short.
[[nodiscard]] QRect placePopup(QPoint cursor, QSize popup, QRect screen, PopupPositionMode mode, int offset);

// The same placement, moved off avoid where a placement off it exists inside screen.
//
// A pixel source that composites MaruPop's own windows into a grab reads the card back on the
// next scan, so a card sitting over the paragraph it answers for removes that paragraph from the
// text the next lookup runs over. avoid is the logical
// bounding box of the paragraph the card is showing.
//
// The rule: place the card by placePopup(); where the result misses avoid, answer it. Where it
// overlaps, take the four bands of screen outside avoid -- above, below, left, right -- keep the
// ones the card fits in, place the card in each by clamping the unconstrained placement into the
// band, and answer the candidate whose centre is nearest the unconstrained placement's centre. A
// tie is broken in the order above, below, left, right. Where no band fits the card, answer the
// unconstrained placement, because a card outside the screen is worse than a card over the text.
//
// An empty avoid answers placePopup() unchanged.
[[nodiscard]] QRect
placePopupAvoiding(QPoint cursor, QSize popup, QRect screen, PopupPositionMode mode, int offset, QRect avoid);

} // namespace maru::popup
