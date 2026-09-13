// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// A capture::FrameSource that answers every grab with an image the size of the region requested,
// carrying that region's origin in its first pixels.
//
// It exists beside capture::FakeFrameSource, which src/capture/framesource.h declares and which
// answers every grab with one image the caller supplied. The difference is the coordinate space a
// suite works in: FakeFrameSource ties the recognized text to the frame, so the text follows the
// pointer and every region finds the same characters, while this one crops a page that is fixed on
// the desktop, so the characters under the pointer and the size of the region are independent. The
// scan-region ladder, the anchor lag and the detector-resolution gate are all measured against the
// second shape.
//
// A fake backend is handed a QImage and nothing else, so the image is what says where it came
// from: cropImage() writes the crop origin into the first six bytes and cropOriginOf() reads it
// back. That also gives the property scan::ScanCache is keyed on, since one region hashes one way
// and any other region another.
#pragma once

#include "capture/framesource.h"

#include <QImage>
#include <QList>
#include <QRect>

namespace maru::test
{

// Bytes of the crop origin an image carries, three for each of x and y, which addresses a desktop
// coordinate up to 16777215.
inline constexpr qsizetype kCropOriginBytes = 6;

// A white image of logical.size() carrying logical.topLeft() in its first kCropOriginBytes bytes.
// QImage::Format_RGB888 pads each scanline to a 4-byte boundary, so a region under 2 px wide holds
// 4 bytes rather than 6; such a region is returned without an origin, and cropOriginOf() answers
// 0,0 for it.
[[nodiscard]] QImage cropImage(QRect logical);

// The origin cropImage() wrote, or 0,0 for an image too narrow to carry one.
[[nodiscard]] QPoint cropOriginOf(const QImage &image);

class CropFrameSource : public capture::FrameSource
{
    Q_OBJECT

public:
    explicit CropFrameSource(QObject *parent = nullptr);
    ~CropFrameSource() override;

    // Milliseconds between grab() and frameReady(), through a QTimer rather than a sleep, so the
    // GUI thread stays live exactly as it does while KWin's D-Bus reply is outstanding. The
    // default of 0 still delivers through the event loop.
    void setDelayMs(int delayMs);

    void grab(const QRect &logical) override;

    // Every grab() call, including the ones capture::FrameSource::beginRequest() coalesces into
    // the pending slot, and the subset that reached a reply. The two differ whenever the caller
    // asks again while a reply is outstanding, so an assertion about compositor traffic reads
    // issued and one about the caller's behaviour reads requested.
    QList<QRect> requested;
    QList<QRect> issued;

private:
    void issue(QRect logical);

    int m_delayMs = 0;
};

} // namespace maru::test
