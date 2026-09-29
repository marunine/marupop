// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// LockWatcher over the Windows Terminal Services session notifications.
//
// On Windows 8 and later, the SessionFlags field of WTSINFOEXW holds WTS_SESSIONSTATE_LOCK or
// WTS_SESSIONSTATE_UNLOCK.
#pragma once

#include "cursor/lockwatcher.h"

#include <memory>

namespace maru::win32
{
class MessageWindow;
}

namespace maru::cursor
{

class WinLockWatcher final : public LockWatcher
{
    Q_OBJECT

public:
    explicit WinLockWatcher(QObject *parent = nullptr);
    ~WinLockWatcher() override;

    // Re-reads the session state.
    void query() override;

    // Applies a WM_WTSSESSION_CHANGE code. Public for a test, which drives the lock state without
    // locking its session.
    void handleSessionChange(unsigned code);

private:
    std::unique_ptr<win32::MessageWindow> m_window;
    bool m_registered = false;
};

} // namespace maru::cursor
