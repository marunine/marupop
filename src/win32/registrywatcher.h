// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// A key under HKEY_CURRENT_USER watched for value changes.
//
// RegNotifyChangeKeyValue signals an event once per registration. Windows discards a registration
// when the registering thread exits, so a RegistryWatcher belongs on a thread that runs for the
// life of the process, such as the GUI thread.
#pragma once

#include <QObject>
#include <QString>

class QWinEventNotifier;

namespace maru::win32
{

class RegistryWatcher : public QObject
{
    Q_OBJECT

public:
    // subKey is relative to HKEY_CURRENT_USER. A key that fails to open is logged, and the
    // watcher stays inactive.
    explicit RegistryWatcher(const QString &subKey, QObject *parent = nullptr);
    ~RegistryWatcher() override;

Q_SIGNALS:
    // A value of the key was written, added or deleted.
    void changed();

private:
    [[nodiscard]] bool arm();

    void *m_key = nullptr;
    void *m_event = nullptr;
    QWinEventNotifier *m_notifier = nullptr;
};

} // namespace maru::win32
