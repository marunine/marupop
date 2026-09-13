// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "privatebus.h"

#include <QDBusConnection>
#include <QLatin1StringView>
#include <QStringLiteral>
#include <QtEnvironmentVariables>

namespace maru::test
{

bool privateBusAvailable()
{
    if (qEnvironmentVariable("MARUPOP_TEST_PRIVATE_BUS") != QLatin1StringView("1")) {
        return false;
    }
    return QDBusConnection::sessionBus().isConnected();
}

QString privateBusSkipReason()
{
    if (qEnvironmentVariable("MARUPOP_TEST_PRIVATE_BUS") != QLatin1StringView("1")) {
        return QStringLiteral("MARUPOP_TEST_PRIVATE_BUS is unset, so a fake service would claim a "
                              "well-known name on a shared bus; run this binary through "
                              "maru_add_gtest_dbus(), which wraps it in dbus-run-session");
    }
    return QStringLiteral("MARUPOP_TEST_PRIVATE_BUS=1 is set, and the session bus is disconnected");
}

} // namespace maru::test
