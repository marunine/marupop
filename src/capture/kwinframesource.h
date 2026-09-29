// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#pragma once

#include "capture/framesource.h"

#include <QPointer>

namespace maru::capture
{

class KWinGrab;
class KWinGrabber;

// FrameSource over org.kde.KWin.ScreenShot2 CaptureArea, with include-cursor false (so a
// pointer moving over static text leaves the frame hash unchanged), hide-caller-windows true
// (so MaruPop's own popup never reaches the OCR pass) and native-resolution true (so a HiDPI
// output is read at its own pixel density).
class KWinFrameSource : public FrameSource
{
    Q_OBJECT

public:
    explicit KWinFrameSource(QObject *parent = nullptr);
    ~KWinFrameSource() override;

    void grab(const QRect &logical) override;

    // True when org.kde.KWin owns its bus name.
    [[nodiscard]] static bool available();

private:
    void issue(const QRect &logical);
    void deliver(const QImage &source, const QRect &logical);
    void issueNext();

    KWinGrabber *m_grabber;
    QPointer<KWinGrab> m_grab;
};

} // namespace maru::capture
