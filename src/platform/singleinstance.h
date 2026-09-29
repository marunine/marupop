// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// One resident process per Windows session, in the role of KDBusService(Unique) on Linux.
//
// CreateMutexW answers ERROR_ALREADY_EXISTS in every process after the primary. The QLocalServer
// pipe accepts connections from the same user alone. A later process sends its arguments and
// working folder as one JSON object ended by a newline, and the primary acknowledges with a
// newline. The pipe name carries the session id, because pipe names are global to the machine and
// Local\ mutex names are per session. The primary holds the mutex until the SingleInstance
// destructor, which runs after Application and its global shortcuts are destroyed.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QLocalServer;

namespace maru::platform
{

class SingleInstance : public QObject
{
    Q_OBJECT

public:
    // Claims the instance for this process, or forwards arguments to the process that holds it.
    explicit SingleInstance(const QStringList &arguments, QObject *parent = nullptr);
    ~SingleInstance() override;

    // True where this process holds the instance or CreateMutexW failed. False where the primary
    // acknowledged the forwarded arguments or the 10 s claim timeout expired. The caller exits on
    // false.
    [[nodiscard]] bool isPrimary() const;

    // The name the mutex and the pipe derive from, for a test that runs two instances.
    [[nodiscard]] static QString instanceName();

Q_SIGNALS:
    // Emitted for each later process, with its arguments and its working folder.
    void activateRequested(const QStringList &arguments, const QString &workingDirectory);

private:
    void listen();
    // Sends the arguments over one connection. True once the primary acknowledged them.
    [[nodiscard]] static bool forward(const QByteArray &payload);

    void *m_mutex = nullptr;
    bool m_primary = false;
    QLocalServer *m_server = nullptr;
};

} // namespace maru::platform
