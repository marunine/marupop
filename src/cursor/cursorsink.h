// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The D-Bus object the KWin relay script calls. data/kwin-script/marupopcursor/contents/code/
// main.js sends one Update per pump tick and reads the reply as its rate control:
// true selects the 8 ms pump, false the 500 ms heartbeat.
#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QPoint>
#include <QString>

class QScreen;

namespace maru::cursor
{

class CursorSink : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.marunine.marupop.CursorSink")

public:
    explicit CursorSink(QObject *parent = nullptr);
    ~CursorSink() override;

    // "/Cursor" and "io.github.marunine.marupop.CursorSink", the two names main.js addresses.
    [[nodiscard]] static QString objectPath();
    [[nodiscard]] static QString interfaceName();

    // Exports the object at objectPath() with ExportScriptableSlots, which is what puts Update
    // on the bus. False when the path is already taken on connection.
    [[nodiscard]] bool registerObject(const QDBusConnection &connection);
    void unregisterObject();
    [[nodiscard]] bool isRegistered() const;

    // The value Update returns to the script.
    void setTracking(bool tracking);
    [[nodiscard]] bool isTracking() const;

    // Delivery latency from the script's Date.now() timestamp to this slot's wall clock.
    // Clock adjustments and timestamp precision can produce negative samples.
    struct LatencyStats
    {
        quint64 samples = 0;
        double lastMs = 0;
        double minMs = 0;
        double maxMs = 0;
        double meanMs = 0;
    };

    [[nodiscard]] LatencyStats latency() const;
    void resetLatency();

    [[nodiscard]] QPoint lastPosition() const;
    [[nodiscard]] QScreen *lastScreen() const;
    // The devicePixelRatio the script read from KWin's LogicalOutput for the last position.
    [[nodiscard]] double lastDevicePixelRatio() const;

public Q_SLOTS:
    // The relay method. Its upper-case name matches main.js.
    // KWin infers D-Bus signatures from JS values: an integral devicePixelRatio can become int32,
    // while a fractional ratio becomes double. Both overloads are needed. x and y are int32
    // (workspace.cursorPos is a QPoint); the epoch-millisecond timestamp is outside int32 range.
    // NOLINTNEXTLINE(readability-identifier-naming)
    Q_SCRIPTABLE bool Update(int x, int y, const QString &screenName, double devicePixelRatio, double sentMs);
    // NOLINTNEXTLINE(readability-identifier-naming)
    Q_SCRIPTABLE bool Update(int x, int y, const QString &screenName, int devicePixelRatio, double sentMs);

Q_SIGNALS:
    void positionChanged(QPoint logical, QScreen *screen);

private:
    [[nodiscard]] QScreen *resolveScreen(QPoint logical, const QString &screenName) const;

    // A connection with a name no bus answers, until registerObject() assigns the one the
    // object is exported on. QDBusConnection has no default constructor.
    QDBusConnection m_connection{QString{}};
    bool m_registered = false;
    bool m_tracking = false;
    QPoint m_lastPosition;
    bool m_havePosition = false;
    QScreen *m_lastScreen = nullptr;
    double m_lastDevicePixelRatio = 1.0;
    LatencyStats m_latency;
    double m_latencySum = 0;
};

} // namespace maru::cursor
