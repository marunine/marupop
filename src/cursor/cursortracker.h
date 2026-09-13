// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The pointer-position source the scan controller reads.
// wl_pointer reports coordinates only over the client's own surfaces. Plasma's implementation
// uses KWinScriptRelay to obtain global positions; the interface keeps callers independent
// of the compositor-specific transport.
#pragma once

#include <QObject>
#include <QPoint>
#include <QString>

#include <utility>

class QScreen;

namespace maru::cursor
{

class CursorTracker : public QObject
{
    Q_OBJECT

public:
    explicit CursorTracker(QObject *parent = nullptr);
    ~CursorTracker() override;

    // While tracking is false the implementation may report positions at a reduced rate or stop
    // reporting them. The KWin relay drops from an 8 ms pump to a 500 ms heartbeat.
    virtual void setTracking(bool tracking);
    [[nodiscard]] bool isTracking() const;

Q_SIGNALS:
    // logical is in logical global desktop coordinates, the space QScreen::geometry() uses.
    // screen is the QScreen under logical, or nullptr for a position on no output.
    void positionChanged(QPoint logical, QScreen *screen);
    // reason is a translated sentence naming what is missing, for a status line.
    void availabilityChanged(bool available, const QString &reason);

private:
    bool m_tracking = false;
};

// The tracker a session with no recognized pointer source gets. It reports one
// availabilityChanged(false, reason) from the event loop and no position, which is what puts the
// reason in the tray tooltip instead of leaving the scan loop waiting for a sample that never
// arrives.
class UnavailableTracker : public CursorTracker
{
    Q_OBJECT

public:
    // reason is a translated sentence naming what is missing.
    explicit UnavailableTracker(QString reason, QObject *parent = nullptr);
    ~UnavailableTracker() override;

    // Reports the reason again. KWinScriptRelay reports its own failure from every start(), and
    // an application that drops the startup report because scanning was off would otherwise
    // never hear it: Backend::startTracking() has nothing to call on this class.
    void setTracking(bool tracking) override;

private:
    void report();

    QString m_reason;
    // Whether availabilityChanged() has been queued. report() emits once for the life of the
    // tracker; the reason cannot change, and the failure is reported to the user.
    bool m_reported = false;
};

} // namespace maru::cursor
