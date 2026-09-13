// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// LockWatcher over hyprland_lock_notify_v1.
//
// hyprland_lock_notifier_v1.get_lock_notification answers with an object carrying locked and
// unlocked, and the protocol states that a session already locked when the request is made
// receives locked immediately (third_party/protocols/hyprland-lock-notify-v1.xml). query() is
// therefore one roundtrip rather than a method call, and isAvailable() reports whether the
// compositor advertises the global at all.
#pragma once

#include "cursor/lockwatcher.h"

struct hyprland_lock_notification_v1;
struct hyprland_lock_notifier_v1;

namespace maru::cursor
{

class WlrLockWatcher : public LockWatcher
{
    Q_OBJECT

public:
    explicit WlrLockWatcher(QObject *parent = nullptr);
    ~WlrLockWatcher() override;

    // One wl_display_roundtrip(), which delivers a locked event the compositor already queued.
    void query() override;

    // True where hyprland_lock_notifier_v1 was bound. A false answer leaves isLocked() false for
    // the life of the object, which is the behaviour a session with no lock source has.
    [[nodiscard]] bool isAvailable() const;

    // True where the compositor advertises hyprland_lock_notifier_v1, without binding it.
    [[nodiscard]] static bool available();

private:
    // The body of query(), called from the constructor as well. A virtual call inside a
    // constructor dispatches to this class whatever the dynamic type is, so the constructor
    // names the implementation rather than the virtual.
    void runQuery();
    void onLocked();
    void onUnlocked();

    hyprland_lock_notifier_v1 *m_notifier = nullptr;
    hyprland_lock_notification_v1 *m_notification = nullptr;

    friend struct LockNotificationListener;
};

} // namespace maru::cursor
