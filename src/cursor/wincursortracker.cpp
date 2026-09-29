// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "cursor/wincursortracker.h"

#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

namespace maru::cursor
{

WinCursorTracker::WinCursorTracker(QObject *parent)
    : CursorTracker(parent)
    , m_timer(new QTimer(this))
{
    // On Windows, a Qt::CoarseTimer fires on the 15.6 ms system tick. A Qt::PreciseTimer keeps
    // the 8 ms interval.
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &WinCursorTracker::poll);
}

WinCursorTracker::~WinCursorTracker() = default;

void WinCursorTracker::start()
{
    if (!m_reported) {
        // Queued, so a caller that connects after the first start() receives the report.
        m_reported = true;
        QTimer::singleShot(0, this, [this] {
            Q_EMIT availabilityChanged(true, QString{});
        });
    }
    m_timer->start(interval());
    poll();
}

void WinCursorTracker::stop()
{
    m_timer->stop();
}

void WinCursorTracker::setTracking(bool tracking)
{
    if (tracking == isTracking()) {
        return;
    }
    CursorTracker::setTracking(tracking);
    if (!m_timer->isActive()) {
        return;
    }
    // QTimer::setInterval() restarts an active timer with the new interval.
    m_timer->setInterval(interval());
    if (tracking) {
        // The first sample after tracking is turned on decides where the first scan runs. An
        // immediate poll saves up to one 500 ms idle interval.
        poll();
    }
}

quint64 WinCursorTracker::sampleCount() const
{
    return m_samples;
}

int WinCursorTracker::interval() const
{
    return isTracking() ? kTrackingPollIntervalMs : kIdlePollIntervalMs;
}

void WinCursorTracker::poll()
{
    ++m_samples;
    const QPoint position = QCursor::pos();
    if (position == m_last) {
        return;
    }
    m_last = position;
    Q_EMIT positionChanged(position, QGuiApplication::screenAt(position));
}

} // namespace maru::cursor
