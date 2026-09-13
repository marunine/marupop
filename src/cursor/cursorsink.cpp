// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "cursor/cursorsink.h"

#include "core/logging.h"

#include <QDBusError>
#include <QDateTime>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QScreen>

#include <algorithm>

namespace maru::cursor
{

CursorSink::CursorSink(QObject *parent)
    : QObject(parent)
{}

CursorSink::~CursorSink()
{
    unregisterObject();
}

QString CursorSink::objectPath()
{
    return QStringLiteral("/Cursor");
}

QString CursorSink::interfaceName()
{
    return QStringLiteral("io.github.marunine.marupop.CursorSink");
}

bool CursorSink::registerObject(const QDBusConnection &connection)
{
    if (m_registered) {
        unregisterObject();
    }
    m_connection = connection;
    // ExportScriptableSlots exports the one slot marked Q_SCRIPTABLE under the interface name
    // Q_CLASSINFO declares. ExportAllSlots would put every slot of every base class on the bus
    // under a generated interface name.
    m_registered = m_connection.registerObject(objectPath(), this, QDBusConnection::ExportScriptableSlots);
    if (!m_registered) {
        qCWarning(logCursor) << "registering" << objectPath() << "failed:" << m_connection.lastError().message();
    }
    return m_registered;
}

void CursorSink::unregisterObject()
{
    if (!m_registered) {
        return;
    }
    m_connection.unregisterObject(objectPath());
    m_registered = false;
}

bool CursorSink::isRegistered() const
{
    return m_registered;
}

void CursorSink::setTracking(bool tracking)
{
    m_tracking = tracking;
}

bool CursorSink::isTracking() const
{
    return m_tracking;
}

CursorSink::LatencyStats CursorSink::latency() const
{
    return m_latency;
}

void CursorSink::resetLatency()
{
    m_latency = LatencyStats{};
    m_latencySum = 0;
}

QPoint CursorSink::lastPosition() const
{
    return m_lastPosition;
}

QScreen *CursorSink::lastScreen() const
{
    return m_lastScreen;
}

double CursorSink::lastDevicePixelRatio() const
{
    return m_lastDevicePixelRatio;
}

QScreen *CursorSink::resolveScreen(QPoint logical, const QString &screenName) const
{
    // workspace.cursorPos and QScreen::geometry() are the same logical global coordinate space:
    // KWin's XdgOutputV1Interface publishes LogicalOutput::geometryF() as xdg_output's
    // logical_position and logical_size, which is what Qt builds QScreen::geometry() from. screenAt() is therefore the
    // primary answer.
    QScreen *screen = QGuiApplication::screenAt(logical);
    if (screen != nullptr && screen->name() == screenName) {
        return screen;
    }
    // The script also ships workspace.screenAt(p).name. A disagreement between the two is a
    // layout Qt and KWin describe differently, which a mirrored or multi-GPU setup can produce;
    // the named output is the one KWin resolved the position against.
    const QList<QScreen *> screens = QGuiApplication::screens();
    const auto named = std::ranges::find_if(screens, [&screenName](const QScreen *candidate) {
        return candidate->name() == screenName;
    });
    if (named != screens.cend()) {
        if (screen != nullptr) {
            qCDebug(logCursor) << "screenAt reports" << screen->name() << "for" << logical << "and KWin reports"
                               << screenName;
        }
        return *named;
    }
    return screen;
}

bool CursorSink::Update(int x, int y, const QString &screenName, int devicePixelRatio, double sentMs)
{
    return Update(x, y, screenName, static_cast<double>(devicePixelRatio), sentMs);
}

bool CursorSink::Update(int x, int y, const QString &screenName, double devicePixelRatio, double sentMs)
{
    const auto now = static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    const double delta = now - sentMs;
    m_latency.lastMs = delta;
    m_latency.minMs = m_latency.samples == 0 ? delta : std::min(m_latency.minMs, delta);
    m_latency.maxMs = m_latency.samples == 0 ? delta : std::max(m_latency.maxMs, delta);
    ++m_latency.samples;
    m_latencySum += delta;
    m_latency.meanMs = m_latencySum / static_cast<double>(m_latency.samples);

    const QPoint logical{x, y};
    m_lastDevicePixelRatio = devicePixelRatio;
    if (m_havePosition && logical == m_lastPosition) {
        // The script already drops an unchanged position while it is active. The heartbeat it
        // sends while tracking is off repeats the last position, and that repeat stops here.
        return m_tracking;
    }
    m_havePosition = true;
    m_lastPosition = logical;
    m_lastScreen = resolveScreen(logical, screenName);
    Q_EMIT positionChanged(logical, m_lastScreen);
    return m_tracking;
}

} // namespace maru::cursor
