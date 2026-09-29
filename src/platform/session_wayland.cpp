// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "cursor/hyprsocket.h"
#include "platform/session_p.h"
#include "wayland/registry.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QFileInfo>
#include <QString>

namespace maru::platform
{

namespace
{

// The three probes, in the order detect() runs them. Each is cheap: one cached D-Bus name list,
// one stat(2), one hash lookup in the registry the process already bound.

bool kwinOwnsItsName()
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return false;
    }
    return bus.interface()->isServiceRegistered(QStringLiteral("org.kde.KWin")).value();
}

bool hyprlandSocketExists()
{
    const QString path = cursor::hyprSocketPath();
    return !path.isEmpty() && QFileInfo::exists(path);
}

bool wlrScreencopyAdvertised()
{
    wl::Registry *registry = wl::Registry::instance();
    return registry != nullptr && registry->has(QByteArrayLiteral("zwlr_screencopy_manager_v1"));
}

} // namespace

Session detectNative()
{
    // Check Hyprland first: its instance signature and socket identify the compositor used by
    // this process. A nested compositor may inherit a Plasma session bus, so merely finding
    // org.kde.KWin on that bus does not identify the current Wayland compositor.
    if (hyprlandSocketExists()) {
        return Session::Hyprland;
    }
    if (kwinOwnsItsName()) {
        return Session::KdePlasma;
    }
    // Last, because a Hyprland session advertises zwlr_screencopy_manager_v1 as well and a Plasma
    // session running a wlroots-protocol bridge would answer this probe too.
    if (wlrScreencopyAdvertised()) {
        return Session::Wlroots;
    }
    return Session::Unknown;
}

} // namespace maru::platform
