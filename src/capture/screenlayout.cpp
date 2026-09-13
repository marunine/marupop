// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "capture/screenlayout.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace maru::capture
{

qreal ScreenLayout::dpx(qreal dpr)
{
    return 1 / dpr;
}

qreal ScreenLayout::dprRound(qreal value, qreal dpr)
{
    return std::round(value * dpr) / dpr;
}

QPointF ScreenLayout::dprRound(const QPointF &point, qreal dpr)
{
    return {dprRound(point.x(), dpr), dprRound(point.y(), dpr)};
}

QRectF ScreenLayout::dprRound(const QRectF &rect, qreal dpr)
{
    const qreal left = dprRound(rect.left(), dpr);
    const qreal top = dprRound(rect.top(), dpr);
    return {QPointF{left, top}, QPointF{dprRound(rect.right(), dpr), dprRound(rect.bottom(), dpr)}};
}

qreal ScreenLayout::dprCeil(qreal value, qreal dpr)
{
    return std::ceil(value * dpr) / dpr;
}

qreal ScreenLayout::dprFloor(qreal value, qreal dpr)
{
    return std::floor(value * dpr) / dpr;
}

QRectF ScreenLayout::boundingRect(const QList<ScreenSlice> &slices)
{
    QRectF rect;
    for (const ScreenSlice &slice : slices) {
        rect |= slice.logicalRect;
    }
    return rect;
}

const ScreenSlice *ScreenLayout::sliceAt(const QList<ScreenSlice> &slices, const QPointF &pos)
{
    for (const ScreenSlice &slice : slices) {
        if (slice.logicalRect.contains(pos)) {
            return &slice;
        }
    }
    return nullptr;
}

const ScreenSlice *ScreenLayout::sliceContaining(const QList<ScreenSlice> &slices, const QRectF &rect)
{
    for (const ScreenSlice &slice : slices) {
        if (slice.logicalRect.contains(rect)) {
            return &slice;
        }
    }
    return nullptr;
}

QImage ScreenLayout::combinedImage(const QList<ScreenSlice> &slices)
{
    if (slices.isEmpty()) {
        return {};
    }
    if (slices.size() == 1) {
        return slices.constFirst().image;
    }
    const QRectF bounds = boundingRect(slices);
    qreal maxDpr = 0;
    for (const ScreenSlice &slice : slices) {
        maxDpr = std::max(maxDpr, slice.image.devicePixelRatio());
    }
    const bool allSameDpr = std::ranges::all_of(slices, [maxDpr](const ScreenSlice &slice) {
        return slice.image.devicePixelRatio() == maxDpr;
    });
    // Ceil to the next integer so integer-DPR sources stay crisp under a fractional max.
    const qreal targetDpr = allSameDpr ? maxDpr : std::ceil(maxDpr);

    QImage combined{(bounds.size() * targetDpr).toSize(), QImage::Format_RGBA8888_Premultiplied};
    combined.fill(Qt::transparent);
    QPainter painter(&combined);
    for (const ScreenSlice &slice : slices) {
        const QPointF pos = (slice.logicalRect.topLeft() - bounds.topLeft()) * targetDpr;
        const QSize size = (slice.logicalRect.size() * targetDpr).toSize();
        const qreal sliceDpr = slice.image.devicePixelRatio();
        const bool integerDpr = static_cast<int>(sliceDpr) == sliceDpr;
        const auto interpolation = integerDpr ? Qt::FastTransformation : Qt::SmoothTransformation;
        painter.drawImage(QRectF{pos, size},
                          size == slice.image.size() ? slice.image
                                                     : slice.image.scaled(size, Qt::KeepAspectRatio, interpolation));
    }
    painter.end();
    // Set the DPR only after painting so it cannot affect QPainter coordinates (Spectacle).
    combined.setDevicePixelRatio(targetDpr);
    return combined;
}

QImage ScreenLayout::cropLogical(const QList<ScreenSlice> &slices, const QRectF &logicalRect)
{
    if (slices.isEmpty() || logicalRect.isEmpty()) {
        return {};
    }
    if (const ScreenSlice *slice = sliceContaining(slices, logicalRect)) {
        const qreal dpr = slice->image.devicePixelRatio();
        const QRectF local = logicalRect.translated(-slice->logicalRect.topLeft());
        const QRect source{QPoint{qRound(local.left() * dpr), qRound(local.top() * dpr)},
                           QSize{qRound(local.width() * dpr), qRound(local.height() * dpr)}};
        QImage cropped = slice->image.copy(source);
        cropped.setDevicePixelRatio(dpr);
        return cropped;
    }
    const QImage combined = combinedImage(slices);
    if (combined.isNull()) {
        return {};
    }
    const qreal dpr = combined.devicePixelRatio();
    const QRectF local = logicalRect.translated(-boundingRect(slices).topLeft());
    const QRect source{QPoint{qRound(local.left() * dpr), qRound(local.top() * dpr)},
                       QSize{qRound(local.width() * dpr), qRound(local.height() * dpr)}};
    QImage cropped = combined.copy(source & combined.rect());
    cropped.setDevicePixelRatio(dpr);
    return cropped;
}

QColor ScreenLayout::colorAt(const QList<ScreenSlice> &slices, const QPointF &logicalPos)
{
    const ScreenSlice *slice = sliceAt(slices, logicalPos);
    if (slice == nullptr) {
        return {};
    }
    const qreal dpr = slice->image.devicePixelRatio();
    const QPointF local = (logicalPos - slice->logicalRect.topLeft()) * dpr;
    const QPoint pixel{std::clamp(static_cast<int>(local.x()), 0, slice->image.width() - 1),
                       std::clamp(static_cast<int>(local.y()), 0, slice->image.height() - 1)};
    return slice->image.pixelColor(pixel);
}

} // namespace maru::capture
