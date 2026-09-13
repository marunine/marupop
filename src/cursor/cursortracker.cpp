// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "cursor/cursortracker.h"

#include <QTimer>

namespace maru::cursor
{

CursorTracker::CursorTracker(QObject *parent)
    : QObject(parent)
{}

CursorTracker::~CursorTracker() = default;

void CursorTracker::setTracking(bool tracking)
{
    m_tracking = tracking;
}

bool CursorTracker::isTracking() const
{
    return m_tracking;
}

UnavailableTracker::UnavailableTracker(QString reason, QObject *parent)
    : CursorTracker(parent)
    , m_reason(std::move(reason))
{
    report();
}

UnavailableTracker::~UnavailableTracker() = default;

void UnavailableTracker::setTracking(bool tracking)
{
    const bool changed = tracking != isTracking();
    CursorTracker::setTracking(tracking);
    if (tracking && changed) {
        report();
    }
}

void UnavailableTracker::report()
{
    // Once. The reason is fixed at construction and nothing here can make the pointer source
    // appear, so a second emission carries no news -- and Application::reportFailure() does not
    // deduplicate, so it would write the tray status twice and raise a second identical
    // notification. Both paths into this function run at startup on a session with no pointer
    // source: the constructor, from platform::Backend::build(), and setTracking(true), from the
    // first ScanController::updateActivity().
    if (m_reported) {
        return;
    }
    m_reported = true;
    // Through the event loop, so a caller that connects to availabilityChanged() after
    // constructing the tracker still receives it.
    QTimer::singleShot(0, this, [this] {
        Q_EMIT availabilityChanged(false, m_reason);
    });
}

} // namespace maru::cursor
