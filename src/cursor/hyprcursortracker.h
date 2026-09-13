// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// CursorTracker over Hyprland's socket1 cursorpos command.
// The client polls every 8 ms while tracking and every 500 ms while idle. One connection
// per sample matches the command's reply-and-close behavior. The timeout bounds requests
// when the compositor stops answering.
#pragma once

#include "cursor/cursortracker.h"

#include <QLocalSocket>
#include <QPoint>
#include <QString>

class QTimer;

namespace maru::cursor
{

class HyprCursorTracker : public CursorTracker
{
    Q_OBJECT

public:
    explicit HyprCursorTracker(QObject *parent = nullptr);
    // The same tracker against a supplied socket path, which is how a test drives it against
    // FakeHyprSocket without setting HYPRLAND_INSTANCE_SIGNATURE for the whole process.
    explicit HyprCursorTracker(QString socketPath, QObject *parent = nullptr);
    ~HyprCursorTracker() override;

    // Starts the poll and reports the outcome through CursorTracker::availabilityChanged(). A
    // socket path that does not exist reports unavailable and keeps retrying at the idle rate,
    // so a Hyprland restart is picked up without restarting marupop.
    void start();
    // Stops the poll. The tracker stays constructed and start() resumes it.
    void stop();

    void setTracking(bool tracking) override;

    [[nodiscard]] bool isAvailable() const;
    // Empty while isAvailable() is true.
    [[nodiscard]] QString unavailableReason() const;
    [[nodiscard]] QString socketPath() const;

    // The last position the socket answered with, in logical global desktop coordinates.
    [[nodiscard]] QPoint lastPosition() const;
    // Samples the socket answered since start(), for a measurement and for a test.
    [[nodiscard]] quint64 sampleCount() const;

    // The poll interval while tracking is true, in milliseconds. 8 by default, which is the
    // interval the KWin relay pumps at.
    void setTrackingIntervalMs(int milliseconds);
    // The poll interval while tracking is false, in milliseconds. 500 by default, which is the
    // interval the KWin relay's heartbeat runs at.
    void setIdleIntervalMs(int milliseconds);

private:
    void poll();
    // Parses whatever the peer sent and delivers or fails it, exactly once per request.
    void completeRequest();
    // Closes the socket and drops whatever arrived. Clears the in-flight flag first, so the
    // disconnected() and errorOccurred() this emits do not re-enter completeRequest().
    void closeSocket();
    void onSocketError(QLocalSocket::LocalSocketError error);
    // Counts one unanswered poll and withdraws availability once enough have run together.
    void recordFailure();
    void applyInterval();
    void setAvailable(bool available, const QString &reason);

    QString m_socketPath;
    QTimer *m_timer = nullptr;
    QLocalSocket *m_socket = nullptr;
    QByteArray m_reply;
    bool m_available = false;
    bool m_started = false;
    // A request has been issued and neither answered nor abandoned. It is what makes the
    // completion path run once per request, whichever of disconnected() and
    // errorOccurred(PeerClosedError) arrives first.
    bool m_requestActive = false;
    // When the in-flight request was issued, for the timeout in poll().
    qint64 m_issuedAtMs = 0;
    QString m_reason;
    QPoint m_last;
    bool m_haveLast = false;
    quint64 m_samples = 0;
    int m_trackingIntervalMs = 8;
    int m_idleIntervalMs = 500;
    // Consecutive failed polls, so one warning is logged per outage rather than one per poll.
    int m_failures = 0;
};

} // namespace maru::cursor
