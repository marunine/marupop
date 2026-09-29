// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "platform/backend.h"

#include "capture/framesource.h"
#include "cursor/cursortracker.h"

namespace maru::platform
{

Backend::Backend(QObject *parent)
    : Backend(detect(), parent)
{}

Backend::Backend(Session session, QObject *parent)
    : QObject(parent)
    , m_session(session)
{
    build();
}

Backend::~Backend() = default;

Session Backend::session() const
{
    return m_session;
}

cursor::CursorTracker *Backend::tracker() const
{
    return m_tracker;
}

capture::FrameSource *Backend::frames() const
{
    return m_frames;
}

cursor::LockWatcher *Backend::lockWatcher() const
{
    return m_lockWatcher;
}

ShortcutRegistry *Backend::shortcuts() const
{
    return m_shortcuts;
}

void Backend::startTracking()
{
    m_tracker->start();
}

void Backend::stopTracking()
{
    m_tracker->stop();
    m_frames->release();
}

bool Backend::capturesOwnWindows() const
{
    return m_frames != nullptr && m_frames->capturesOwnWindows();
}

} // namespace maru::platform
