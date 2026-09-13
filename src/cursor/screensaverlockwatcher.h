// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// LockWatcher over org.freedesktop.ScreenSaver.
// The watcher reads GetActive and listens for ActiveChanged(bool) at /ScreenSaver,
// trying the freedesktop service and Plasma's org.kde.screensaver alias.
#pragma once

#include "cursor/lockwatcher.h"

#include <QString>

namespace maru::cursor
{

class ScreenSaverLockWatcher : public LockWatcher
{
    Q_OBJECT

public:
    explicit ScreenSaverLockWatcher(QObject *parent = nullptr);
    ~ScreenSaverLockWatcher() override;

    // Re-reads GetActive. Called once from the constructor.
    void query() override;

private Q_SLOTS:
    // A slot because QDBusConnection::connect() is string-based and takes SLOT().
    void setLocked(bool locked);

private:
    void queryService(const QString &service);
};

} // namespace maru::cursor
