// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "cursor/screensaverlockwatcher.h"

#include "core/logging.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>

namespace maru::cursor
{

namespace
{

constexpr QLatin1StringView kFreedesktopService("org.freedesktop.ScreenSaver");
constexpr QLatin1StringView kKdeService("org.kde.screensaver");
constexpr QLatin1StringView kInterface("org.freedesktop.ScreenSaver");
constexpr QLatin1StringView kShortPath("/ScreenSaver");
constexpr QLatin1StringView kLongPath("/org/freedesktop/ScreenSaver");
constexpr int kCallTimeoutMs = 5000;

} // namespace

ScreenSaverLockWatcher::ScreenSaverLockWatcher(QObject *parent)
    : LockWatcher(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    // An empty service matches ActiveChanged from any sender, which covers both bus names one
    // kwin_wayland process owns without subscribing twice to the same signal. Both object paths
    // are subscribed because the specification names /org/freedesktop/ScreenSaver and Plasma
    // exports /ScreenSaver.
    const QLatin1StringView paths[] = {kShortPath, kLongPath};
    for (const QLatin1StringView path : paths) {
        bus.connect(QString{},
                    QString{path},
                    QString{kInterface},
                    QStringLiteral("ActiveChanged"),
                    this,
                    SLOT(setLocked(bool)));
    }
    // queryService() rather than query(): a virtual call inside a constructor dispatches to this
    // class whatever the dynamic type is, so naming the implementation says what runs.
    queryService(QString{kFreedesktopService});
}

ScreenSaverLockWatcher::~ScreenSaverLockWatcher() = default;

void ScreenSaverLockWatcher::query()
{
    queryService(QString{kFreedesktopService});
}

void ScreenSaverLockWatcher::queryService(const QString &service)
{
    const QDBusMessage message =
        QDBusMessage::createMethodCall(service, QString{kShortPath}, QString{kInterface}, QStringLiteral("GetActive"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, kCallTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, service](QDBusPendingCallWatcher *self) {
        const QDBusPendingReply<bool> reply = *self;
        self->deleteLater();
        if (!reply.isError()) {
            setLocked(reply.value());
            return;
        }
        if (service == QLatin1StringView(kFreedesktopService)) {
            // Plasma owns both names from one process; a session that answers neither leaves the
            // watcher reporting unlocked, which is the state that keeps scanning working.
            queryService(QString{kKdeService});
            return;
        }
        qCDebug(logCursor) << "no screen saver service answered GetActive:" << reply.error().message();
    });
}

void ScreenSaverLockWatcher::setLocked(bool locked)
{
    setLockedState(locked);
}

} // namespace maru::cursor
