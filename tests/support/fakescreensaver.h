// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// A stand-in for org.freedesktop.ScreenSaver at /ScreenSaver on a private session bus, which is
// what makes the locked branch of cursor::LockWatcher reachable.
//
// kwin_wayland owns that name on a Plasma session and answers GetActive with the session's real
// state, so a test on the developer's bus asserts whatever the screen happens to be doing. The
// fake sets the state instead, and emits ActiveChanged(bool) on every transition, which is the
// signal LockWatcher connects to.
//
// Registration is refused on any bus other than a private one; see privatebus.h.
#pragma once

#include <QDBusContext>
#include <QObject>
#include <QString>

namespace maru::test
{

// QObject and QDBusContext are the two bases Qt requires for an exported object that raises a
// D-Bus error: QDBusContext::sendErrorReply() reads the message the connection is dispatching.
// NOLINTNEXTLINE(misc-multiple-inheritance)
class FakeScreenSaver : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.ScreenSaver")

public:
    explicit FakeScreenSaver(QObject *parent = nullptr);
    ~FakeScreenSaver() override;

    [[nodiscard]] bool isRegistered() const;

    // The reason isRegistered() is false, for a GTEST_SKIP() message.
    [[nodiscard]] QString skipReason() const;

    // Sets the state GetActive answers and emits ActiveChanged(active) on a change. A repeated
    // value emits nothing, which is what a compositor does.
    void setActive(bool active);

    // The number of GetActive calls answered, which distinguishes a watcher that queried once
    // from one that queried on every request.
    [[nodiscard]] int getActiveCount() const;

    // Answers GetActive with org.freedesktop.DBus.Error.Failed while true, which is the reply a
    // bus with a name but no working implementation produces. Defaults to false.
    bool failGetActive = false;

public Q_SLOTS:
    // NOLINTBEGIN(readability-identifier-naming)
    // The three method names org.freedesktop.ScreenSaver defines. D-Bus dispatches on the exact
    // name, so the project's camelBack rule cannot apply to them.
    Q_SCRIPTABLE bool GetActive();
    Q_SCRIPTABLE bool SetActive(bool active);
    Q_SCRIPTABLE void Lock();
    // NOLINTEND(readability-identifier-naming)

Q_SIGNALS:
    // NOLINTNEXTLINE(readability-identifier-naming)
    Q_SCRIPTABLE void ActiveChanged(bool active);

private:
    bool m_active = false;
    bool m_registered = false;
    int m_getActiveCount = 0;
    QString m_skipReason;
};

} // namespace maru::test
