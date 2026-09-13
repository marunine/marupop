// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "platform/session.h"

#include "core/logging.h"
#include "cursor/hyprsocket.h"
#include "wayland/registry.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QFileInfo>
#include <QString>

#include <KLocalizedString>

#include <optional>

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

Session detectUncached()
{
    const QByteArray override = qgetenv("MARUPOP_PLATFORM");
    if (!override.isEmpty()) {
        bool recognized = false;
        const Session session = parseSessionId(QString::fromLocal8Bit(override), &recognized);
        if (recognized) {
            qCInfo(logPlatform) << "MARUPOP_PLATFORM selects the" << sessionId(session) << "backends";
            return session;
        }
        qCWarning(logPlatform) << "MARUPOP_PLATFORM holds" << override << "which names no session; detecting instead";
    }

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

std::optional<Session> cachedSession;

} // namespace

Session detect()
{
    if (!cachedSession.has_value()) {
        cachedSession = detectUncached();
        qCInfo(logPlatform) << "session detected as" << sessionId(*cachedSession);
    }
    return *cachedSession;
}

Session redetect()
{
    cachedSession.reset();
    return detect();
}

QString sessionId(Session session)
{
    switch (session) {
    case Session::KdePlasma:
        return QStringLiteral("kde");
    case Session::Hyprland:
        return QStringLiteral("hyprland");
    case Session::Wlroots:
        return QStringLiteral("wlroots");
    case Session::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

Session parseSessionId(const QString &id, bool *recognized)
{
    const QString lowered = id.trimmed().toLower();
    const auto answer = [recognized](Session session, bool known) {
        if (recognized != nullptr) {
            *recognized = known;
        }
        return session;
    };
    if (lowered == QLatin1String("kde") || lowered == QLatin1String("plasma")) {
        return answer(Session::KdePlasma, true);
    }
    if (lowered == QLatin1String("hyprland")) {
        return answer(Session::Hyprland, true);
    }
    if (lowered == QLatin1String("wlroots")) {
        return answer(Session::Wlroots, true);
    }
    if (lowered == QLatin1String("unknown")) {
        return answer(Session::Unknown, true);
    }
    return answer(Session::Unknown, false);
}

QString sessionName(Session session)
{
    switch (session) {
    case Session::KdePlasma:
        return i18n("KDE Plasma");
    case Session::Hyprland:
        return i18n("Hyprland");
    case Session::Wlroots:
        return i18n("wlroots");
    case Session::Unknown:
        break;
    }
    return i18n("unrecognized");
}

bool capturesOwnWindows(Session session)
{
    switch (session) {
    case Session::Hyprland:
    case Session::Wlroots:
        return true;
    case Session::KdePlasma:
    case Session::Unknown:
        break;
    }
    return false;
}

} // namespace maru::platform
