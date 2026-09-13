// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Copied from marusnap/src/capture/screenlayout.{h,cpp} into the maru::capture namespace. The
// logical-to-physical rounding helpers are what a caller uses to align a scan rect or a popup
// rect to the hardware grid of the output it lands on; the slice helpers cover a frozen
// multi-output capture, which is the shape ScreenLayout was written against.
#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

namespace maru::capture
{

// One frozen per-screen grab. The image's devicePixelRatio() reflects the screen scale;
// logicalRect is the screen's geometry in logical desktop coordinates (Wayland: identical to
// QScreen::geometry()).
struct ScreenSlice
{
    QImage image;
    QRectF logicalRect;
    QString screenName;
};

// Geometry helpers for the capture-then-display overlay (Spectacle's Geometry/combinedImage
// approach). Everything works in logical desktop coordinates; hardware alignment is achieved
// by rounding in physical space and converting back.
class ScreenLayout
{
public:
    // Logical size of one hardware pixel.
    [[nodiscard]] static qreal dpx(qreal dpr);
    [[nodiscard]] static qreal dprRound(qreal value, qreal dpr);
    [[nodiscard]] static QPointF dprRound(const QPointF &point, qreal dpr);
    // Rounds all four edges independently so the rect stays pixel-aligned.
    [[nodiscard]] static QRectF dprRound(const QRectF &rect, qreal dpr);
    [[nodiscard]] static qreal dprCeil(qreal value, qreal dpr);
    [[nodiscard]] static qreal dprFloor(qreal value, qreal dpr);

    [[nodiscard]] static QRectF boundingRect(const QList<ScreenSlice> &slices);
    [[nodiscard]] static const ScreenSlice *sliceAt(const QList<ScreenSlice> &slices, const QPointF &pos);
    [[nodiscard]] static const ScreenSlice *sliceContaining(const QList<ScreenSlice> &slices, const QRectF &rect);

    // One premultiplied-RGBA image covering the bounding rect of every slice. Same-DPR
    // layouts composite 1:1; mixed-DPI layouts scale up to ceil(max DPR), Fast for
    // integer-DPR sources and Smooth for fractional ones (Spectacle's combinedImage()).
    [[nodiscard]] static QImage combinedImage(const QList<ScreenSlice> &slices);

    // Crop a logical desktop rect out of the frozen capture. A rect within a single slice is
    // cropped at that screen's native DPR; spans fall back to cropping combinedImage().
    [[nodiscard]] static QImage cropLogical(const QList<ScreenSlice> &slices, const QRectF &logicalRect);

    // Color of the hardware pixel under a logical desktop position; invalid outside every slice.
    [[nodiscard]] static QColor colorAt(const QList<ScreenSlice> &slices, const QPointF &logicalPos);
};

} // namespace maru::capture
