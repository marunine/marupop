// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "cursor/lockwatcher.h"

#include "core/logging.h"

namespace maru::cursor
{

LockWatcher::LockWatcher(QObject *parent)
    : QObject(parent)
{}

LockWatcher::~LockWatcher() = default;

bool LockWatcher::isLocked() const
{
    return m_locked;
}

void LockWatcher::setLockedState(bool locked)
{
    if (locked == m_locked) {
        return;
    }
    m_locked = locked;
    qCDebug(logCursor) << "session lock state:" << locked;
    Q_EMIT lockedChanged(locked);
}

} // namespace maru::cursor
