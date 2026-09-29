// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "cursor/winlockwatcher.h"

#include "core/logging.h"
#include "win32/messagewindow.h"

#include <windows.h>
#include <wtsapi32.h>

namespace maru::cursor
{

WinLockWatcher::WinLockWatcher(QObject *parent)
    : LockWatcher(parent)
    , m_window(std::make_unique<win32::MessageWindow>([this](unsigned message, quintptr wParam, qintptr) {
        if (message != WM_WTSSESSION_CHANGE) {
            return false;
        }
        handleSessionChange(static_cast<unsigned>(wParam));
        return true;
    }))
{
    auto *window = static_cast<HWND>(m_window->handle());
    if (window != nullptr) {
        m_registered = WTSRegisterSessionNotification(window, NOTIFY_FOR_THIS_SESSION) != FALSE;
        if (!m_registered) {
            qCWarning(logCursor) << "WTSRegisterSessionNotification failed with error" << GetLastError()
                                 << "; the lock state is read once at startup";
        }
    }
    // Qualified, because a virtual call from a constructor resolves to the class under
    // construction.
    WinLockWatcher::query();
}

WinLockWatcher::~WinLockWatcher()
{
    if (m_registered) {
        WTSUnRegisterSessionNotification(static_cast<HWND>(m_window->handle()));
    }
}

void WinLockWatcher::query()
{
    LPWSTR buffer = nullptr;
    DWORD bytes = 0;
    if (WTSQuerySessionInformationW(
            WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION, WTSSessionInfoEx, &buffer, &bytes) == FALSE) {
        qCWarning(logCursor) << "WTSQuerySessionInformation failed with error" << GetLastError();
        return;
    }
    const auto *info = reinterpret_cast<const WTSINFOEXW *>(buffer);
    if (bytes >= sizeof(WTSINFOEXW) && info->Level == 1) {
        const LONG flags = info->Data.WTSInfoExLevel1.SessionFlags;
        if (flags == WTS_SESSIONSTATE_LOCK) {
            setLockedState(true);
        } else if (flags == WTS_SESSIONSTATE_UNLOCK) {
            setLockedState(false);
        }
    }
    WTSFreeMemory(buffer);
}

void WinLockWatcher::handleSessionChange(unsigned code)
{
    switch (code) {
    case WTS_SESSION_LOCK:
        setLockedState(true);
        break;
    case WTS_SESSION_UNLOCK:
        setLockedState(false);
        break;
    default:
        break;
    }
}

} // namespace maru::cursor
