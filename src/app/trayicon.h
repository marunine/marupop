// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#pragma once

#include <QObject>
#include <QString>

class KStatusNotifierItem;
class QAction;
class QMenu;

namespace maru
{

// The tray item, which is the whole chrome of the application: there is no main window. A left
// click toggles scanning, a middle click toggles the lookup window, a right click opens the
// menu. The item emits requests and runs no job itself; Application decides what each one does.
class TrayIcon : public QObject
{
    Q_OBJECT

public:
    explicit TrayIcon(QObject *parent = nullptr);

    // The menu the item shows on a secondary click, holding six entries: Scanning, Lookup Window,
    // Manage Dictionaries, Configure MaruPop, About MaruPop and Quit.
    [[nodiscard]] QMenu *contextMenu() const;

    void setVisible(bool visible);
    // Moves the menu check, the icon and the tooltip subtitle onto the scanning state.
    void setScanning(bool scanning);
    // Moves the check of the Lookup Window entry onto the window's visibility.
    void setLookupWindowVisible(bool visible);
    // Last failure, shown as the tooltip subtitle instead of a modal dialog. Also carries the
    // short confirmation a hotkey with no other visible effect leaves behind; an empty string
    // clears it.
    void setStatusMessage(const QString &message);
    // The scan controller's own status line ("Scanning · meikiocr", "Paused (screen locked)"),
    // which the subtitle falls back to while no failure is set.
    void setStatusText(const QString &status);

Q_SIGNALS:
    void toggleScanningRequested();
    void lookupWindowRequested();
    void dictionariesRequested();
    void settingsRequested();
    void aboutRequested();
    void quitRequested();

private:
    void updateToolTip();

    KStatusNotifierItem *m_item = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_scanningAction = nullptr;
    QAction *m_lookupWindowAction = nullptr;
    QString m_statusMessage;
    QString m_statusText;
    bool m_scanning = false;
};

} // namespace maru
