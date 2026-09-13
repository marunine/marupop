// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "cursor/hyprcursortracker.h"

#include "core/logging.h"
#include "cursor/hyprsocket.h"

#include <QDateTime>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocalSocket>
#include <QScreen>
#include <QTimer>

#include <KLocalizedString>

namespace maru::cursor
{

namespace
{

// The socket1 command. No trailing newline: hyprctl writes the bare command bytes and
// the socket1 protocol accepts the bare command without a line delimiter.
constexpr QByteArrayView kCursorPosCommand("cursorpos");

// A request outstanding for longer than this is abandoned and counted as a failure. Four idle
// intervals, so a compositor that is busy for two seconds is reported once rather than at every
// tick.
constexpr int kRequestTimeoutMultiplier = 4;

// Consecutive failures before availability is withdrawn. One failed poll is a compositor that
// was busy; sixteen at the tracking rate is 128 ms of silence and at the idle rate is 8 s.
constexpr int kFailuresBeforeUnavailable = 16;

} // namespace

HyprCursorTracker::HyprCursorTracker(QObject *parent)
    : HyprCursorTracker(hyprSocketPath(), parent)
{}

HyprCursorTracker::HyprCursorTracker(QString socketPath, QObject *parent)
    : CursorTracker(parent)
    , m_socketPath(std::move(socketPath))
{
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &HyprCursorTracker::poll);

    m_socket = new QLocalSocket(this);
    connect(m_socket, &QLocalSocket::connected, this, [this] {
        m_socket->write(kCursorPosCommand.data(), kCursorPosCommand.size());
        m_socket->flush();
    });
    connect(m_socket, &QLocalSocket::readyRead, this, [this] {
        m_reply.append(m_socket->readAll());
    });
    // The peer closes after replying. QLocalSocket can signal PeerClosedError before
    // disconnection, so both signals share a completion path that runs once per request.
    connect(m_socket, &QLocalSocket::disconnected, this, &HyprCursorTracker::completeRequest);
    connect(m_socket, &QLocalSocket::errorOccurred, this, &HyprCursorTracker::onSocketError);
}

HyprCursorTracker::~HyprCursorTracker()
{
    if (m_socket != nullptr) {
        m_socket->abort();
    }
}

void HyprCursorTracker::start()
{
    if (m_socketPath.isEmpty()) {
        setAvailable(false, i18n("Pointer tracking unavailable: HYPRLAND_INSTANCE_SIGNATURE is unset."));
        return;
    }
    m_started = true;
    applyInterval();
    poll();
}

void HyprCursorTracker::stop()
{
    m_started = false;
    m_timer->stop();
    closeSocket();
}

void HyprCursorTracker::setTracking(bool tracking)
{
    if (tracking == isTracking()) {
        return;
    }
    CursorTracker::setTracking(tracking);
    applyInterval();
    if (tracking && m_started) {
        // The first sample after tracking is turned on decides where the first scan runs, so it
        // is taken now rather than one idle interval later.
        poll();
    }
}

bool HyprCursorTracker::isAvailable() const
{
    return m_available;
}

QString HyprCursorTracker::unavailableReason() const
{
    return m_available ? QString{} : m_reason;
}

QString HyprCursorTracker::socketPath() const
{
    return m_socketPath;
}

QPoint HyprCursorTracker::lastPosition() const
{
    return m_last;
}

quint64 HyprCursorTracker::sampleCount() const
{
    return m_samples;
}

void HyprCursorTracker::setTrackingIntervalMs(int milliseconds)
{
    m_trackingIntervalMs = qMax(1, milliseconds);
    applyInterval();
}

void HyprCursorTracker::setIdleIntervalMs(int milliseconds)
{
    m_idleIntervalMs = qMax(1, milliseconds);
    applyInterval();
}

void HyprCursorTracker::applyInterval()
{
    const int interval = isTracking() ? m_trackingIntervalMs : m_idleIntervalMs;
    m_timer->setInterval(interval);
    if (m_started) {
        m_timer->start();
    }
}

void HyprCursorTracker::poll()
{
    if (m_requestActive) {
        // A request is still in flight. Abandoning it after the timeout keeps one stuck
        // connection from stopping the poll for the life of the process.
        const int timeoutMs = m_timer->interval() * kRequestTimeoutMultiplier;
        if (m_issuedAtMs + timeoutMs < QDateTime::currentMSecsSinceEpoch()) {
            m_requestActive = false;
            closeSocket();
            recordFailure();
        }
        return;
    }

    closeSocket();
    m_requestActive = true;
    m_issuedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_socket->connectToServer(m_socketPath, QIODevice::ReadWrite);
}

void HyprCursorTracker::completeRequest()
{
    if (!m_requestActive) {
        // The close this handler is running for is one closeSocket() made, or a second signal
        // for a request that already completed. Either way there is nothing left to answer.
        return;
    }
    m_requestActive = false;

    // Anything the peer wrote before closing is still buffered on this side.
    m_reply.append(m_socket->readAll());
    const std::optional<QPoint> position = parseCursorPos(m_reply);
    closeSocket();

    if (!position.has_value()) {
        recordFailure();
        return;
    }

    m_failures = 0;
    ++m_samples;
    setAvailable(true, QString{});

    if (m_haveLast && *position == m_last) {
        return;
    }
    m_last = *position;
    m_haveLast = true;
    Q_EMIT positionChanged(m_last, QGuiApplication::screenAt(m_last));
}

void HyprCursorTracker::closeSocket()
{
    // abort() emits disconnected() for a socket that was connected, and can emit
    // errorOccurred() as well. m_requestActive is already false by the time this runs on the
    // completion paths, so neither re-enters; the assignment below covers the paths that call it
    // before the flag is cleared.
    m_requestActive = false;
    m_socket->abort();
    m_reply.clear();
}

void HyprCursorTracker::onSocketError(QLocalSocket::LocalSocketError error)
{
    if (error == QLocalSocket::PeerClosedError) {
        // The ordinary end of a cursorpos reply: the peer answered and closed.
        // Consume buffered reply bytes before treating the request as complete.
        completeRequest();
        return;
    }
    if (!m_requestActive) {
        return;
    }
    m_requestActive = false;
    closeSocket();
    recordFailure();
}

void HyprCursorTracker::recordFailure()
{
    ++m_failures;
    if (m_failures < kFailuresBeforeUnavailable) {
        return;
    }
    if (!QFileInfo::exists(m_socketPath)) {
        setAvailable(false, i18n("Hyprland socket not found: %1.", m_socketPath));
        return;
    }
    setAvailable(false, i18n("Could not read the pointer position from %1.", m_socketPath));
}

void HyprCursorTracker::setAvailable(bool available, const QString &reason)
{
    if (available == m_available && reason == m_reason) {
        return;
    }
    m_available = available;
    m_reason = reason;
    if (!available) {
        qCWarning(logCursor) << "the Hyprland pointer source is unavailable:" << reason;
    }
    Q_EMIT availabilityChanged(m_available, m_reason);
}

} // namespace maru::cursor
