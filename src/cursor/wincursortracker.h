// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// CursorTracker over QCursor::pos() on Windows.
//
// GetCursorPos returns the global pointer position to every Win32 process. QCursor::pos() calls
// GetCursorPos and maps the result from device pixels to logical coordinates of the screen under
// it. The poll runs at kTrackingPollIntervalMs while tracking and at kIdlePollIntervalMs while
// idle. positionChanged() is emitted for a changed position only.
#pragma once

#include "cursor/cursortracker.h"

#include <QPoint>

#include <optional>

class QTimer;

namespace maru::cursor
{

class WinCursorTracker : public CursorTracker
{
    Q_OBJECT

public:
    explicit WinCursorTracker(QObject *parent = nullptr);
    ~WinCursorTracker() override;

    // Starts the poll and reports the pointer source available. start() takes the first sample
    // immediately.
    void start() override;
    // Stops the poll. start() resumes it.
    void stop() override;

    void setTracking(bool tracking) override;

    // Samples taken since construction, for a measurement and for a test.
    [[nodiscard]] quint64 sampleCount() const;

private:
    void poll();
    [[nodiscard]] int interval() const;

    // Active between start() and stop().
    QTimer *m_timer = nullptr;
    std::optional<QPoint> m_last;
    bool m_reported = false;
    quint64 m_samples = 0;
};

} // namespace maru::cursor
