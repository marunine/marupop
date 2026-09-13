// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "cropframesource.h"

#include <QTimer>

namespace maru::test
{

QImage cropImage(QRect logical)
{
    QImage image{logical.size(), QImage::Format_RGB888};
    image.fill(Qt::white);
    if (image.bytesPerLine() < kCropOriginBytes) {
        return image;
    }
    uchar *first = image.scanLine(0);
    first[0] = static_cast<uchar>(logical.x() & 0xFF);
    first[1] = static_cast<uchar>((logical.x() >> 8) & 0xFF);
    first[2] = static_cast<uchar>((logical.x() >> 16) & 0xFF);
    first[3] = static_cast<uchar>(logical.y() & 0xFF);
    first[4] = static_cast<uchar>((logical.y() >> 8) & 0xFF);
    first[5] = static_cast<uchar>((logical.y() >> 16) & 0xFF);
    return image;
}

QPoint cropOriginOf(const QImage &image)
{
    if (image.bytesPerLine() < kCropOriginBytes) {
        return {};
    }
    const uchar *first = image.constScanLine(0);
    return QPoint{first[0] | (first[1] << 8) | (first[2] << 16), first[3] | (first[4] << 8) | (first[5] << 16)};
}

CropFrameSource::CropFrameSource(QObject *parent)
    : capture::FrameSource(parent)
{}

CropFrameSource::~CropFrameSource() = default;

void CropFrameSource::setDelayMs(int delayMs)
{
    m_delayMs = delayMs;
}

void CropFrameSource::grab(const QRect &logical)
{
    requested.append(logical);
    if (const std::optional<QRect> rect = beginRequest(logical)) {
        issue(*rect);
    }
}

void CropFrameSource::issue(QRect logical)
{
    issued.append(logical);
    QTimer::singleShot(m_delayMs, this, [this, logical] {
        capture::Frame frame;
        frame.logicalRect = logical;
        frame.image = cropImage(logical);
        frame.scale = 1.0;
        frame.hash = hashImage(frame.image);
        frame.grabMs = requestElapsedMs();
        Q_EMIT frameReady(frame);
        // After frameReady(), so a grab() made from the handler is the rect endRequest() returns.
        if (const std::optional<QRect> next = endRequest()) {
            issue(*next);
        }
    });
}

} // namespace maru::test
