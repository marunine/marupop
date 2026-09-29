// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "win32/messagewindow.h"

#include "core/logging.h"

#include <windows.h>

namespace maru::win32
{

namespace
{

constexpr wchar_t kClassName[] = L"MaruPopMessageWindow";

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    return static_cast<LRESULT>(
        MessageWindow::dispatch(window, message, static_cast<quintptr>(wParam), static_cast<qintptr>(lParam)));
}

// RegisterClassExW fails with ERROR_CLASS_ALREADY_EXISTS for a second registration from the same
// module.
bool registerClass()
{
    static const bool registered = [] {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = windowProcedure;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = kClassName;
        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            qCWarning(logWin32) << "RegisterClassExW failed with error" << GetLastError();
            return false;
        }
        return true;
    }();
    return registered;
}

} // namespace

MessageWindow::MessageWindow(Handler handler)
    : m_handler(std::move(handler))
{
    if (!registerClass()) {
        return;
    }
    HWND window =
        CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (window == nullptr) {
        qCWarning(logWin32) << "CreateWindowExW failed with error" << GetLastError();
        return;
    }
    // GWLP_USERDATA is set after creation, so WM_NCCREATE and WM_CREATE reach DefWindowProcW
    // and skip m_handler.
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    m_window = window;
}

MessageWindow::~MessageWindow()
{
    if (m_window != nullptr) {
        auto *window = static_cast<HWND>(m_window);
        // GWLP_USERDATA is cleared before DestroyWindow, so WM_DESTROY and WM_NCDESTROY reach
        // DefWindowProcW and skip m_handler.
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        DestroyWindow(window);
    }
}

void *MessageWindow::handle() const
{
    return m_window;
}

qintptr MessageWindow::dispatch(void *window, unsigned message, quintptr wParam, qintptr lParam)
{
    auto *hwnd = static_cast<HWND>(window);
    auto *self =
        reinterpret_cast<MessageWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); // NOLINT(performance-no-int-to-ptr)
    if (self != nullptr && self->m_handler && self->m_handler(message, wParam, lParam)) {
        return 0;
    }
    return static_cast<qintptr>(
        DefWindowProcW(hwnd, message, static_cast<WPARAM>(wParam), static_cast<LPARAM>(lParam)));
}

} // namespace maru::win32
