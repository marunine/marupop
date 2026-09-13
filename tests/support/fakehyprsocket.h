// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// A stand-in for Hyprland's socket1, which is what makes cursor::HyprCursorTracker reachable on
// a host running no Hyprland.
//
// The fake owns the real path rather than a test-only one: it listens on
// <runtimeDir>/hypr/<signature>/.socket.sock, the path cursor::hyprSocketPath() builds, under a
// runtime directory the suite supplies. Production code therefore carries no test hook, which is
// the rule the D-Bus fakes follow with the real well-known names.
//
// The protocol is the one CSocket1 answers: the peer writes a command, the server writes the
// reply and closes (Hyprland 0.56.0, src/ipc/s1/Unix.cpp:110 and src/ipc/s1/Commands.cpp:1968). Only the commands a
// suite asks for are answered; anything else is answered with "unknown request", which is Hyprland's own text.
#pragma once

#include <QObject>
#include <QPoint>
#include <QString>

class QLocalServer;

namespace maru::test
{

class FakeHyprSocket : public QObject
{
    Q_OBJECT

public:
    // Creates <runtimeDir>/hypr/<signature>/ and listens on .socket.sock inside it.
    FakeHyprSocket(QString runtimeDir, QString instanceSignature, QObject *parent = nullptr);
    ~FakeHyprSocket() override;

    [[nodiscard]] bool isListening() const;
    // The reason isListening() is false, for a GTEST_SKIP() message.
    [[nodiscard]] QString skipReason() const;
    // The path the server is listening on, which is what a tracker is pointed at.
    [[nodiscard]] QString socketPath() const;

    // The point `cursorpos` answers with. Read on every request, so a suite moves the pointer
    // between two polls by assigning it.
    void setCursorPos(QPoint position);
    [[nodiscard]] QPoint cursorPos() const;

    // Answers `cursorpos` in the JSON form the `j/` prefix selects rather than the plain
    // "<x>, <y>" form. False by default.
    void setJsonReplies(bool json);

    // Answers every request by closing the connection with no reply, which is what a compositor
    // that is shutting down does. False by default.
    void setSilent(bool silent);

    // Delays every reply by this many milliseconds, which is what a compositor whose main loop is
    // busy does. 0 by default. A delay longer than the client's own timeout is what drives the
    // abandoned-request path.
    void setReplyDelayMs(int milliseconds);

    // Requests accepted since construction, whatever the reply was.
    [[nodiscard]] int requestCount() const;
    // The commands received, in arrival order.
    [[nodiscard]] QStringList commands() const;

    // Stops listening and removes the socket file, which is what a Hyprland exit leaves behind.
    void stop();

private:
    void onNewConnection();

    QLocalServer *m_server = nullptr;
    QString m_path;
    QString m_skipReason;
    QPoint m_cursor;
    bool m_json = false;
    bool m_silent = false;
    int m_replyDelayMs = 0;
    int m_requests = 0;
    QStringList m_commands;
};

} // namespace maru::test
