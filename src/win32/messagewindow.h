// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// A message-only window (HWND_MESSAGE parent) owned by the constructing thread.
//
// Qt's event dispatcher runs the message loop of the constructing thread, so the window procedure
// runs between Qt events on the constructing thread, and the handler can emit signals directly.
#pragma once

#include <QtGlobal>

#include <functional>

namespace maru::win32
{

class MessageWindow
{
public:
    // Called for every message the window receives. true consumes the message, and the window
    // procedure returns 0. false passes the message to DefWindowProcW.
    using Handler = std::function<bool(unsigned message, quintptr wParam, qintptr lParam)>;

    explicit MessageWindow(Handler handler);
    ~MessageWindow();

    MessageWindow(const MessageWindow &) = delete;
    MessageWindow &operator=(const MessageWindow &) = delete;

    // The HWND, or nullptr where RegisterClassExW or CreateWindowExW failed. The void *
    // type keeps <windows.h> out of messagewindow.h.
    [[nodiscard]] void *handle() const;

    // The window procedure body: routes a message to the handler of the MessageWindow that owns
    // window, and to DefWindowProcW where none consumes it.
    static qintptr dispatch(void *window, unsigned message, quintptr wParam, qintptr lParam);

private:
    void *m_window = nullptr;
    Handler m_handler;
};

} // namespace maru::win32
