// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// FrameSource over DXGI Desktop Duplication, with a GDI BitBlt from the screen DC as the
// fallback.
//
// Both paths omit the pointer image. The grabs run on a dedicated thread, because a BitBlt waits
// for the next composition, one refresh interval.
#pragma once

#include "capture/framesource.h"

#include <QRect>

class QScreen;
class QThread;

namespace maru::capture
{

class WinCaptureWorker;

class WinFrameSource : public FrameSource
{
    Q_OBJECT

public:
    enum class Method
    {
        DesktopDuplication,
        BitBlt,
    };

    explicit WinFrameSource(QObject *parent = nullptr);
    ~WinFrameSource() override;

    void grab(const QRect &logical) override;

    // logical clipped to the screen under its centre, because Frame::scale carries the
    // device-pixel ratio of one output.
    [[nodiscard]] QRect quantize(const QRect &logical) const override;

    [[nodiscard]] bool capturesOwnWindows() const override;

    void release() override;

    // false restricts every grab to BitBlt.
    void setDesktopDuplicationEnabled(bool enabled);

    // The path that produced the last frame.
    [[nodiscard]] Method lastMethod() const;

    // The device-pixel rectangle of logical on a screen whose logical geometry is
    // screenLogical and whose device-pixel geometry is screenNative. Edges are rounded
    // independently, which keeps adjacent rectangles adjacent in device pixels.
    [[nodiscard]] static QRect nativeRect(const QRect &logical, const QRect &screenLogical, const QRect &screenNative);

    // The device-pixel geometry of screen, from its HMONITOR, or an empty rect where Qt reports
    // no monitor handle for it.
    [[nodiscard]] static QRect nativeGeometry(const QScreen *screen);

private:
    void issue(const QRect &logical);
    void deliver(const QRect &logical,
                 const QRect &occluded,
                 const QImage &image,
                 qreal scale,
                 Method method,
                 const QString &error);
    void issueNext();

    QThread *m_thread = nullptr;
    WinCaptureWorker *m_worker = nullptr;
    Method m_lastMethod = Method::BitBlt;
    bool m_duplicationEnabled = true;
};

} // namespace maru::capture
