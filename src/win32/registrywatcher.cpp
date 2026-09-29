// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "win32/registrywatcher.h"

#include "core/logging.h"

#include <QWinEventNotifier>

#include <windows.h>

namespace maru::win32
{

RegistryWatcher::RegistryWatcher(const QString &subKey, QObject *parent)
    : QObject(parent)
{
    HKEY key = nullptr;
    const LSTATUS opened =
        RegOpenKeyExW(HKEY_CURRENT_USER, reinterpret_cast<LPCWSTR>(subKey.utf16()), 0, KEY_NOTIFY, &key);
    if (opened != ERROR_SUCCESS) {
        qCWarning(logWin32) << "cannot watch" << subKey << ": RegOpenKeyExW failed with error" << opened;
        return;
    }
    m_key = key;
    m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (m_event == nullptr || !arm()) {
        qCWarning(logWin32) << "cannot watch" << subKey << "; error" << GetLastError();
        return;
    }
    m_notifier = new QWinEventNotifier(m_event, this);
    connect(m_notifier, &QWinEventNotifier::activated, this, [this] {
        // The notification is registered again before changed() is emitted, so a write made by
        // a connected slot is reported as well.
        if (!arm()) {
            m_notifier->setEnabled(false);
        }
        Q_EMIT changed();
    });
}

RegistryWatcher::~RegistryWatcher()
{
    delete m_notifier;
    // RegCloseKey ends the RegNotifyChangeKeyValue registration.
    if (m_key != nullptr) {
        RegCloseKey(static_cast<HKEY>(m_key));
    }
    if (m_event != nullptr) {
        CloseHandle(m_event);
    }
}

bool RegistryWatcher::arm()
{
    return RegNotifyChangeKeyValue(static_cast<HKEY>(m_key), FALSE, REG_NOTIFY_CHANGE_LAST_SET, m_event, TRUE) ==
           ERROR_SUCCESS;
}

} // namespace maru::win32
