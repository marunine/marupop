// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "platform/singleinstance.h"

#include "core/logging.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QThread>

#include <windows.h>

namespace maru::platform
{

namespace
{

// The maximum time a later process spends reaching the primary or claiming the instance. A
// primary refuses connections between CreateMutexW and QLocalServer::listen(). A quitting
// primary holds the mutex until its threads have joined.
constexpr int kClaimTimeoutMs = 10000;
constexpr int kRetryIntervalMs = 50;
constexpr char kAcknowledgement = '\n';

QString pipeName()
{
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    return SingleInstance::instanceName() + QStringLiteral("-") + QString::number(session);
}

} // namespace

QString SingleInstance::instanceName()
{
    // MARUPOP_TEST_INSTANCE separates a test's instances from a resident MaruPop.
    const QString suffix = qEnvironmentVariable("MARUPOP_TEST_INSTANCE");
    return suffix.isEmpty() ? QStringLiteral(MARUPOP_APPLICATION_ID)
                            : QStringLiteral(MARUPOP_APPLICATION_ID) + QStringLiteral("-") + suffix;
}

SingleInstance::SingleInstance(const QStringList &arguments, QObject *parent)
    : QObject(parent)
{
    QJsonObject message;
    message.insert(QStringLiteral("arguments"), QJsonArray::fromStringList(arguments));
    message.insert(QStringLiteral("workingDirectory"), QDir::currentPath());
    const QByteArray payload = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';

    const std::wstring mutexName = QString(QStringLiteral("Local\\") + instanceName()).toStdWString();
    const QDeadlineTimer deadline{kClaimTimeoutMs};
    for (;;) {
        HANDLE mutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
        const DWORD error = GetLastError();
        if (mutex == nullptr) {
            // The process runs as primary, so the application starts at the cost of a possible
            // second resident process.
            qCWarning(logPlatform) << "CreateMutexW failed with error" << error
                                   << "; starting without the instance check";
            m_primary = true;
            return;
        }
        if (error != ERROR_ALREADY_EXISTS) {
            m_mutex = mutex;
            m_primary = true;
            listen();
            return;
        }
        // The handle from CreateMutexW also keeps the mutex object alive, so it is closed before
        // the next claim.
        CloseHandle(mutex);
        if (forward(payload)) {
            return;
        }
        if (deadline.hasExpired()) {
            qCWarning(logPlatform) << "the resident MaruPop did not accept the command line";
            return;
        }
        QThread::msleep(kRetryIntervalMs);
    }
}

SingleInstance::~SingleInstance()
{
    if (m_mutex != nullptr) {
        CloseHandle(m_mutex);
    }
}

bool SingleInstance::isPrimary() const
{
    return m_primary;
}

void SingleInstance::listen()
{
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(pipeName())) {
        qCWarning(logPlatform) << "cannot listen on" << pipeName() << ":" << m_server->errorString();
        return;
    }
    // A connection accepted after the event loop ends stays unread. With the server closed at
    // aboutToQuit, a later process gets a refused connection and retries until the quitting
    // primary exits.
    connect(qApp, &QCoreApplication::aboutToQuit, m_server, &QLocalServer::close);
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server->nextPendingConnection()) {
            // The whole message can arrive before nextPendingConnection() returns the socket. The
            // peer can disconnect before the event loop reports readyRead.
            const auto consume = [this, socket] {
                if (!socket->canReadLine() || socket->property("marupopConsumed").toBool()) {
                    return;
                }
                socket->setProperty("marupopConsumed", true);
                const QJsonObject message = QJsonDocument::fromJson(socket->readLine()).object();
                QStringList arguments;
                const QJsonArray array = message.value(QStringLiteral("arguments")).toArray();
                for (const auto &value : array) {
                    arguments.append(value.toString());
                }
                Q_EMIT activateRequested(arguments, message.value(QStringLiteral("workingDirectory")).toString());
                socket->write(&kAcknowledgement, 1);
                socket->flush();
            };
            connect(socket, &QLocalSocket::readyRead, this, consume);
            connect(socket, &QLocalSocket::disconnected, this, [socket, consume] {
                consume();
                socket->deleteLater();
            });
            consume();
        }
    });
}

bool SingleInstance::forward(const QByteArray &payload)
{
    QLocalSocket socket;
    socket.connectToServer(pipeName());
    if (!socket.waitForConnected(kRetryIntervalMs)) {
        return false;
    }
    socket.write(payload);
    // The exit of a quitting primary breaks the pipe, which ends waitForReadyRead() before
    // kClaimTimeoutMs.
    const bool acknowledged = socket.waitForBytesWritten(kClaimTimeoutMs) && socket.waitForReadyRead(kClaimTimeoutMs) &&
                              socket.read(1) == QByteArray(1, kAcknowledgement);
    socket.disconnectFromServer();
    return acknowledged;
}

} // namespace maru::platform
