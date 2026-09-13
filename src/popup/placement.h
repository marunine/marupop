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
//
// sides, where given, carries the side the card took on the previous placement in and the side it
// takes on this one out. A pointer resting near a point where the mode changes sides would
// otherwise send the card across the pointer on every sample its hand jitters by, so a side the
// previous placement took is kept until the pointer has passed the point by kSideHysteresisPx:
// the flip modes return from a flipped side only once the unflipped card clears the screen edge
// by that much, and VisualNovel keeps its side across the half of the screen within that
// distance of it. The thirds of VisualNovel and the flip conditions themselves are unchanged, so a
// kept side is always one the mode would have chosen a few pixels earlier. A sides whose known
// flag is false places without history, as a null pointer does.
struct PopupSides
{
    bool known = false;
    // The card is left of the pointer: a flip on the horizontal axis.
    bool left = false;
    // The card is above the pointer: a flip on the vertical axis, or VisualNovel's upper side.
    bool above = false;
    // The band of placePopupAvoiding() the card took, in its order above, below, left, right, or
    // -1 where the card took the unconstrained placement.
    int band = -1;
};

// Logical pixels the pointer has to pass a side change by before a card that has a side leaves it.
constexpr int kSideHysteresisPx = 32;

[[nodiscard]] QRect
placePopup(QPoint cursor, QSize popup, QRect screen, PopupPositionMode mode, int offset, PopupSides *sides = nullptr);

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
// An empty avoid answers placePopup() unchanged. sides is passed through to placePopup() and
// records the side the mode chose, before any band moved the card, and the band the card took.
// A band the previous placement took counts as kSideHysteresisPx nearer than it is, so a pointer
// where two bands are about equally near keeps the card in one of them rather than sending it
// across the paragraph on every sample its hand jitters by.
[[nodiscard]] QRect placePopupAvoiding(QPoint cursor,
                                       QSize popup,
                                       QRect screen,
                                       PopupPositionMode mode,
                                       int offset,
                                       QRect avoid,
                                       PopupSides *sides = nullptr);

} // namespace maru::popup
