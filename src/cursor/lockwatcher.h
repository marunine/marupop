// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The session lock state, so scanning stops while the greeter is on screen.
//
// Pointer and capture interfaces are not used as lock-state signals. The scan
// controller must explicitly pause when the lock watcher reports a locked session.
//
// Two implementations answer the same contract. ScreenSaverLockWatcher reads
// org.freedesktop.ScreenSaver, which kwin_wayland owns together with org.kde.screensaver.
// WlrLockWatcher binds hyprland_lock_notifier_v1, whose locked and unlocked events carry the
// same transition and whose get_lock_notification is specified to send locked immediately for a
// session that is already locked.
#pragma once

#include <QObject>

namespace maru::cursor
{

class LockWatcher : public QObject
{
    Q_OBJECT

public:
    explicit LockWatcher(QObject *parent = nullptr);
    ~LockWatcher() override;

    // False until the source reports otherwise, which is the state that lets scanning run on a
    // session bus and on a compositor that answer nothing.
    [[nodiscard]] bool isLocked() const;

    // Re-reads the state from the source. Called once by each implementation's constructor.
    virtual void query() = 0;

Q_SIGNALS:
    void lockedChanged(bool locked);

protected:
    // Records locked and emits lockedChanged() for a change. The one write path, so a
    // subclass never touches the flag directly.
    void setLockedState(bool locked);

private:
    bool m_locked = false;
};

} // namespace maru::cursor
