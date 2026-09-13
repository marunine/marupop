// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Recognition results kept by the region they were recognized from, so a pointer moving inside
// an unchanged region costs a hit test and nothing else.
//
// The key is the pair (logical rect, frame hash) rather than the rect alone: KWin's
// CaptureArea is grabbed with include-cursor false, so the hash of a region holding static
// text stays equal while the pointer moves over it. A matching hash permits reuse of
// recognition for that region; hash equality is a cache heuristic, not a proof of pixel identity.
//
// Eight entries is enough for the three rungs of the progressive ladder at two or three places
// the pointer has visited recently, and small enough that the QImage-free entries cost nothing
// worth measuring.
#pragma once

#include "capture/framesource.h"
#include "ocr/ocrtypes.h"

#include <QRect>

#include <list>

namespace maru::scan
{

// One recognition result and the frame geometry needed to map its boxes back to the desktop.
// The image itself is deliberately not kept: the boxes are what a hit test and a highlight
// need, and eight full frames would be tens of megabytes.
struct CachedScan
{
    QRect logicalRect;
    quint64 hash = 0;
    // Image pixels per logical pixel, from capture::Frame::scale.
    qreal scale = 1.0;
    ocr::Result result;
    // ocr::medianCharExtent(result), in source-image pixels, computed once by insert(). The
    // scan loop reads it on every pointer sample, which is 125 per second, and computing it
    // costs one allocation and one sort over every character box of every paragraph: 2400 of
    // them for the largest page the default MaxScanWidth by MaxScanHeight produces.
    double medianCharExtent = 0.0;
    // capture::Frame::occluded of the frame this entry was recognized from, in logical global
    // desktop coordinates. Empty on a pixel source that excludes MaruPop's own windows, which
    // org.kde.KWin.ScreenShot2 does; the popup's rectangle on a wlroots compositor, which
    // composites the card into every grab it overlaps. A character whose box lies inside it is
    // the card's own rendering or the black the `no_screen_share` layer rule paints over it, and
    // the scan controller refuses it.
    QRect occluded;

    // A frame carrying the geometry alone, which is what capture::imageToLogical() and
    // capture::logicalToImage() read.
    [[nodiscard]] capture::Frame frame() const
    {
        return capture::Frame{
            .image = {}, .logicalRect = logicalRect, .scale = scale, .hash = hash, .grabMs = 0, .occluded = occluded};
    }
};

class ScanCache
{
public:
    explicit ScanCache(int capacity = 8);

    // The entry for exactly this region and these pixels, or nullptr. A hit is promoted to
    // most-recently-used, which is why this is not const: the returned pointer stays valid
    // until the next insert(), invalidate() or clear().
    [[nodiscard]] const CachedScan *find(QRect logicalRect, quint64 hash);

    // Replaces an entry with the same key. Inserting past the capacity drops the
    // least-recently-used entry. occluded is capture::Frame::occluded of the frame the result
    // was recognized from.
    void insert(QRect logicalRect, quint64 hash, qreal scale, ocr::Result result, QRect occluded = {});

    void clear();
    // Drops every entry whose region intersects logicalRect, which is what a caller that knows
    // the screen content changed under a region uses. An empty rect drops nothing.
    void invalidate(QRect logicalRect);

    [[nodiscard]] int size() const;
    [[nodiscard]] int capacity() const;

private:
    // Most-recently-used first. A list rather than a vector because the promotion on a hit is a
    // splice, and eight entries never make the difference either way.
    std::list<CachedScan> m_entries;
    int m_capacity;
};

} // namespace maru::scan
