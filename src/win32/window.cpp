// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "win32/window.h"

#include "core/logging.h"

#include <QGuiApplication>
#include <QOperatingSystemVersion>
#include <QWindow>

#include <windows.h>

namespace maru::win32
{

namespace
{

bool affinityExcludes(HWND window)
{
    DWORD affinity = WDA_NONE;
    return GetWindowDisplayAffinity(window, &affinity) != FALSE && affinity == WDA_EXCLUDEFROMCAPTURE;
}

} // namespace

void *handleOf(QWindow *window)
{
    static const bool nativeWindows = QGuiApplication::platformName() == QLatin1StringView{"windows"};
    if (window == nullptr || !nativeWindows) {
        return nullptr;
    }
    // WId is an integer type holding the HWND the windows plugin created.
    return reinterpret_cast<void *>(window->winId()); // NOLINT(performance-no-int-to-ptr)
}

bool excludeFromCapture(QWindow *window)
{
    auto *hwnd = static_cast<HWND>(handleOf(window));
    if (hwnd == nullptr) {
        return false;
    }
    if (affinityExcludes(hwnd)) {
        return true;
    }
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const bool layered = (style & WS_EX_LAYERED) != 0;
    if (layered) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style & ~static_cast<LONG_PTR>(WS_EX_LAYERED));
    }
    const BOOL set = SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
    const DWORD error = set != FALSE ? ERROR_SUCCESS : GetLastError();
    if (layered) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style);
    }
    const bool excluded = affinityExcludes(hwnd);
    if (!excluded) {
        // Failure causes: a release before Windows 10 version 2004 (build 19041), which accepts
        // WDA_MONITOR only, and a window drawn with UpdateLayeredWindow before
        // excludeFromCapture().
        qCWarning(logWin32) << "SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) failed with error" << error
                            << "; the window stays in screen captures";
    }
    return excluded;
}

bool captureExclusionSupported()
{
    return QOperatingSystemVersion::current() >=
           QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 19041);
}

void raiseTopmost(QWindow *window)
{
    auto *hwnd = static_cast<HWND>(handleOf(window));
    if (hwnd == nullptr) {
        return;
    }
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void *foregroundWindow()
{
    return GetForegroundWindow();
}

bool activate(void *window)
{
    auto *hwnd = static_cast<HWND>(window);
    if (hwnd == nullptr || IsWindow(hwnd) == FALSE) {
        return false;
    }
    if (SetForegroundWindow(hwnd) != FALSE && GetForegroundWindow() == hwnd) {
        return true;
    }
    // A thread attached to the foreground thread's input state shares its foreground right.
    HWND foreground = GetForegroundWindow();
    const DWORD foregroundThread = foreground != nullptr ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    const DWORD thisThread = GetCurrentThreadId();
    const bool attached = foregroundThread != 0 && foregroundThread != thisThread &&
                          AttachThreadInput(thisThread, foregroundThread, TRUE) != FALSE;
    SetForegroundWindow(hwnd);
    if (attached) {
        AttachThreadInput(thisThread, foregroundThread, FALSE);
    }
    const bool active = GetForegroundWindow() == hwnd;
    if (!active) {
        qCDebug(logWin32) << "SetForegroundWindow was refused for" << window;
    }
    return active;
}

} // namespace maru::win32
