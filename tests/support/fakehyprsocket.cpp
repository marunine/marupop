// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "fakehyprsocket.h"

#include "cursor/hyprsocket.h"

#include <QDir>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

namespace maru::test
{

FakeHyprSocket::FakeHyprSocket(QString runtimeDir, QString instanceSignature, QObject *parent)
    : QObject(parent)
    , m_path(cursor::hyprSocketPath(runtimeDir, instanceSignature))
{
    if (m_path.isEmpty()) {
        m_skipReason = QStringLiteral("a runtime directory and an instance signature are both required");
        return;
    }
    const QString directory = runtimeDir + QLatin1String("/hypr/") + instanceSignature;
    if (!QDir().mkpath(directory)) {
        m_skipReason = QStringLiteral("could not create %1").arg(directory);
        return;
    }
    // A stale file from a previous run would make listen() fail; the real compositor bails out
    // on the same condition rather than removing it, but a test directory has no other owner.
    QFile::remove(m_path);

    m_server = new QLocalServer(this);
    connect(m_server, &QLocalServer::newConnection, this, &FakeHyprSocket::onNewConnection);
    if (!m_server->listen(m_path)) {
        m_skipReason = QStringLiteral("could not listen on %1: %2").arg(m_path, m_server->errorString());
        delete m_server;
        m_server = nullptr;
    }
}

FakeHyprSocket::~FakeHyprSocket()
{
    stop();
}

bool FakeHyprSocket::isListening() const
{
    return m_server != nullptr && m_server->isListening();
}

QString FakeHyprSocket::skipReason() const
{
    return m_skipReason;
}

QString FakeHyprSocket::socketPath() const
{
    return m_path;
}

void FakeHyprSocket::setCursorPos(QPoint position)
{
    m_cursor = position;
}

QPoint FakeHyprSocket::cursorPos() const
{
    return m_cursor;
}

void FakeHyprSocket::setJsonReplies(bool json)
{
    m_json = json;
}

void FakeHyprSocket::setSilent(bool silent)
{
    m_silent = silent;
}

void FakeHyprSocket::setReplyDelayMs(int milliseconds)
{
    m_replyDelayMs = milliseconds;
}

int FakeHyprSocket::requestCount() const
{
    return m_requests;
}

QStringList FakeHyprSocket::commands() const
{
    return m_commands;
}

void FakeHyprSocket::stop()
{
    if (m_server != nullptr) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    if (!m_path.isEmpty()) {
        QFile::remove(m_path);
    }
}

void FakeHyprSocket::onNewConnection()
{
    while (QLocalSocket *peer = m_server->nextPendingConnection()) {
        connect(peer, &QLocalSocket::readyRead, this, [this, peer] {
            const QString command = QString::fromUtf8(peer->readAll()).trimmed();
            if (command.isEmpty()) {
                return;
            }
            ++m_requests;
            m_commands.append(command);
            if (m_silent) {
                peer->disconnectFromServer();
                return;
            }
            // The `j/` prefix selects the JSON output format, and the fake answers whichever
            // form setJsonReplies() selected regardless, so a suite can drive the parser's two
            // branches without changing the tracker's command.
            const bool cursorPos = command.endsWith(QLatin1String("cursorpos"));
            QByteArray reply;
            if (!cursorPos) {
                reply = QByteArrayLiteral("unknown request");
            } else if (m_json) {
                reply = QStringLiteral("{\n    \"x\": %1,\n    \"y\": %2\n}\n")
                            .arg(m_cursor.x())
                            .arg(m_cursor.y())
                            .toUtf8();
            } else {
                reply = QStringLiteral("%1, %2").arg(m_cursor.x()).arg(m_cursor.y()).toUtf8();
            }
            if (m_replyDelayMs <= 0) {
                peer->write(reply);
                peer->flush();
                peer->disconnectFromServer();
                return;
            }
            QTimer::singleShot(m_replyDelayMs, peer, [peer, reply] {
                peer->write(reply);
                peer->flush();
                peer->disconnectFromServer();
            });
        });
        connect(peer, &QLocalSocket::disconnected, peer, &QObject::deleteLater);
    }
}

} // namespace maru::test
