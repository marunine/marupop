// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Win32 operations on the native handle of a QWindow: capture exclusion, topmost raising and
// activation.
#pragma once

#include <QtGlobal>

class QWindow;

namespace maru::win32
{

// Sets WDA_EXCLUDEFROMCAPTURE on window. The flag removes window from GDI BitBlt, DXGI Desktop
// Duplication and Windows.Graphics.Capture images, and the monitor still shows window. Returns
// true where GetWindowDisplayAffinity() reads the flag back afterwards.
//
// SetWindowDisplayAffinity fails with ERROR_NOT_ENOUGH_MEMORY (8) on a window drawn through
// UpdateLayeredWindow, which Qt uses to present a Qt::WA_TranslucentBackground window. An
// affinity set while WS_EX_LAYERED is cleared persists after the style is restored, so the call
// clears WS_EX_LAYERED for its duration. Clearing WS_EX_LAYERED on a window already drawn with
// UpdateLayeredWindow blanks the window until its next update, so the call belongs after
// QWindow::winId() and before the first paint. The flag belongs to the HWND. A window Qt
// recreates after a window-flag change needs a second call.
bool excludeFromCapture(QWindow *window);

// True where the running Windows release accepts WDA_EXCLUDEFROMCAPTURE: Windows 10 version 2004
// (build 19041) or later.
[[nodiscard]] bool captureExclusionSupported();

// Moves window to the top of the topmost z-order band and leaves the activation unchanged.
// Another topmost window, such as the taskbar, can move above window after window is shown.
void raiseTopmost(QWindow *window);

// The foreground window of the desktop, as an HWND. QGuiApplication::focusWindow() reports
// windows of the calling process only. The void * handle type keeps <windows.h> out of window.h.
[[nodiscard]] void *foregroundWindow();

// Makes window the foreground window. Returns true where window is foreground afterwards.
// SetForegroundWindow succeeds for the process that received the last input event, such as the
// WM_HOTKEY of a pin shortcut.
bool activate(void *window);

// The HWND of window, created on demand, or nullptr for a null window and under a platform plugin
// other than "windows".
[[nodiscard]] void *handleOf(QWindow *window);

} // namespace maru::win32
