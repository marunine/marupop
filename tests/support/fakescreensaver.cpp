// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "fakescreensaver.h"

#include "privatebus.h"

#include <QDBusConnection>

namespace maru::test
{

namespace
{

constexpr QLatin1StringView kService("org.freedesktop.ScreenSaver");
constexpr QLatin1StringView kPath("/ScreenSaver");

} // namespace

FakeScreenSaver::FakeScreenSaver(QObject *parent)
    : QObject(parent)
{
    if (!privateBusAvailable()) {
        m_skipReason = privateBusSkipReason();
        return;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(
            kPath, this, QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals)) {
        m_skipReason = QStringLiteral("/ScreenSaver was already registered on this bus");
        return;
    }
    if (!bus.registerService(kService)) {
        m_skipReason = QStringLiteral("the bus refused the name org.freedesktop.ScreenSaver, "
                                      "which another process owns");
        return;
    }
    m_registered = true;
}

FakeScreenSaver::~FakeScreenSaver()
{
    if (m_registered) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.unregisterService(kService);
        bus.unregisterObject(kPath);
    }
}

bool FakeScreenSaver::isRegistered() const
{
    return m_registered;
}

QString FakeScreenSaver::skipReason() const
{
    return m_skipReason;
}

void FakeScreenSaver::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    Q_EMIT ActiveChanged(m_active);
}

int FakeScreenSaver::getActiveCount() const
{
    return m_getActiveCount;
}

bool FakeScreenSaver::GetActive()
{
    ++m_getActiveCount;
    if (failGetActive) {
        sendErrorReply(QDBusError::Failed, QStringLiteral("GetActive is unavailable"));
        return false;
    }
    return m_active;
}

bool FakeScreenSaver::SetActive(bool active)
{
    setActive(active);
    return true;
}

void FakeScreenSaver::Lock()
{
    setActive(true);
}

} // namespace maru::test
