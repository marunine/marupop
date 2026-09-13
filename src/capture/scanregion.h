// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The progressive scan rectangle and mappings between source pixels and logical desktop
// coordinates. All functions except workspaceRect() are pure.
// Start with a small rectangle centered on the pointer and grow each dimension when the
// recognized line reaches an edge, up to the configured maximum.
#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>

#include <optional>

namespace maru::capture
{

struct Frame;

// The union of every QScreen::geometry(), in logical global desktop coordinates. Empty when
// QGuiApplication has no screens.
[[nodiscard]] QRect workspaceRect();

// A rect of size, centred on cursor and moved inside workspace. Each dimension of size is
// clamped to the matching dimension of workspace first, so the result is always contained in
// workspace. Empty for an empty size or an empty workspace.
[[nodiscard]] QRect initialRect(QPoint cursor, QSize size, QRect workspace);

// The next rect of the ladder: each dimension of current doubled, capped at maxSize and at the
// matching dimension of workspace, expanded about the centre of current, moved inside
// workspace, and then moved so that cursor is inside it. nullopt when current already covers
// both capped dimensions, which is what tells the caller the ladder is exhausted.
[[nodiscard]] std::optional<QRect> grow(QRect current, QPoint cursor, QSize maxSize, QRect workspace);

// The tile a zwlr_screencopy_v1 region grab of requested uses on the output whose logical
// geometry is output.
//
// Quantization bounds the number of distinct capture boxes sent to the compositor.
// This limits retained session resources on compositors that cache capture sessions
// by client, output and box; moving by one pixel must not create unbounded cache entries.
//
// The grid: the tile keeps the size of requested, each dimension clamped to output; its origin
// lies on a lattice anchored at output.topLeft() with a stride of half the tile size; the index
// chosen is the one that centres the tile nearest to the centre of requested; the result is
// moved inside output. The count of distinct tiles per output per size is therefore
// (2 * output.width() / width + 1) * (2 * output.height() / height + 1), and the centre of
// requested lies at least a quarter of the tile from the tile's edge, which is 120 logical
// pixels horizontally and 67 vertically for a 480x270 tile.
//
// Empty for an empty requested or an empty output.
[[nodiscard]] QRect wlrTileFor(QRect requested, QRect output);

// True when lineBox comes within margin pixels of any edge of frame, which is the signal that
// the recognized text continues outside the captured region. Both rects are in the same
// coordinate space. False for an empty lineBox or an empty frame.
[[nodiscard]] bool touchesEdge(QRect lineBox, QRect frame, int margin);

// Frame image pixels to logical global desktop coordinates. The mapping is
// frame.logicalRect.topLeft() + point / frame.scale.
[[nodiscard]] QPoint imageToLogical(const Frame &frame, QPoint imagePoint);
[[nodiscard]] QRect imageToLogical(const Frame &frame, QRect imageRect);

// Logical global desktop coordinates to Frame image pixels, the inverse of imageToLogical().
// The result is not clamped to the image: a caller that maps a point outside frame.logicalRect
// gets a point outside the image.
[[nodiscard]] QPoint logicalToImage(const Frame &frame, QPoint logicalPoint);
[[nodiscard]] QRect logicalToImage(const Frame &frame, QRect logicalRect);

} // namespace maru::capture
