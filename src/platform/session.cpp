// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "platform/session.h"

#include "core/logging.h"
#include "platform/session_p.h"

#include <QString>

#include <KLocalizedString>

#include <optional>

namespace maru::platform
{

namespace
{

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

    return detectNative();
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
    case Session::Windows:
        return QStringLiteral("windows");
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
    if (lowered == QLatin1String("windows")) {
        return answer(Session::Windows, true);
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
    case Session::Windows:
        return i18n("Windows");
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
    case Session::Windows:
    case Session::Unknown:
        break;
    }
    return false;
}

} // namespace maru::platform
