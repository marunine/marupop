// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "cursor/wlrlockwatcher.h"

#include "core/logging.h"
#include "wayland-hyprland-lock-notify-v1-client-protocol.h"
#include "wayland/registry.h"

namespace maru::cursor
{

namespace
{

constexpr QByteArrayView kNotifierInterface("hyprland_lock_notifier_v1");

} // namespace

// The two hyprland_lock_notification_v1 events, forwarded to the watcher the listener's user
// data names.
struct LockNotificationListener
{
    static void locked(void *data, hyprland_lock_notification_v1 * /*notification*/)
    {
        static_cast<WlrLockWatcher *>(data)->onLocked();
    }

    static void unlocked(void *data, hyprland_lock_notification_v1 * /*notification*/)
    {
        static_cast<WlrLockWatcher *>(data)->onUnlocked();
    }
};

namespace
{

const hyprland_lock_notification_v1_listener kNotificationListener = {
    .locked = &LockNotificationListener::locked,
    .unlocked = &LockNotificationListener::unlocked,
};

} // namespace

bool WlrLockWatcher::available()
{
    wl::Registry *registry = wl::Registry::instance();
    return registry != nullptr && registry->has(kNotifierInterface.toByteArray());
}

WlrLockWatcher::WlrLockWatcher(QObject *parent)
    : LockWatcher(parent)
{
    wl::Registry *registry = wl::Registry::instance();
    if (registry == nullptr) {
        qCWarning(logCursor) << "no Wayland registry; the session lock state stays unlocked";
        return;
    }
    m_notifier = static_cast<hyprland_lock_notifier_v1 *>(registry->bind(&hyprland_lock_notifier_v1_interface, 1));
    if (m_notifier == nullptr) {
        qCInfo(logCursor) << "the compositor advertises no hyprland_lock_notifier_v1;"
                          << "the session lock state stays unlocked";
        return;
    }
    m_notification = hyprland_lock_notifier_v1_get_lock_notification(m_notifier);
    hyprland_lock_notification_v1_add_listener(m_notification, &kNotificationListener, this);
    runQuery();
}

WlrLockWatcher::~WlrLockWatcher()
{
    if (m_notification != nullptr) {
        hyprland_lock_notification_v1_destroy(m_notification);
    }
    if (m_notifier != nullptr) {
        hyprland_lock_notifier_v1_destroy(m_notifier);
    }
}

void WlrLockWatcher::query()
{
    runQuery();
}

void WlrLockWatcher::runQuery()
{
    // get_lock_notification sends locked immediately for a session that is already locked, so a
    // roundtrip is what turns that queued event into a delivered one.
    if (wl::Registry *registry = wl::Registry::instance(); registry != nullptr && m_notification != nullptr) {
        registry->roundtrip();
    }
}

bool WlrLockWatcher::isAvailable() const
{
    return m_notification != nullptr;
}

void WlrLockWatcher::onLocked()
{
    setLockedState(true);
}

void WlrLockWatcher::onUnlocked()
{
    setLockedState(false);
}

} // namespace maru::cursor
