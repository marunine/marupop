// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "app/trayicon.h"

#include "app/appicon.h"

#include <QAction>
#include <QIcon>
#include <QMenu>

#include <KLocalizedString>

#ifdef Q_OS_WIN
#include "win32/registrywatcher.h"

#include <QSystemTrayIcon>
#include <QTimer>
#else
#include <KStatusNotifierItem>
#endif

namespace maru
{

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent)
#ifdef Q_OS_WIN
    , m_item(new QSystemTrayIcon(this))
#else
    , m_item(new KStatusNotifierItem(QStringLiteral("marupop"), this))
#endif
    , m_menu(new QMenu)
{
#ifndef Q_OS_WIN
    m_item->setCategory(KStatusNotifierItem::ApplicationStatus);
    // Active in both scanning states. NeedsAttention is for a condition the user has to act
    // on, and a scanning toggle the user set is not one.
    m_item->setStatus(KStatusNotifierItem::Active);
    m_item->setTitle(i18n("MaruPop"));
    // The standard actions add a Quit entry that would bypass our own teardown.
    m_item->setStandardActionsEnabled(false);
#endif
    updateIcon();

    m_scanningAction = m_menu->addAction(QIcon::fromTheme(QStringLiteral("document-scan")),
                                         i18nc("@action:inmenu", "Enable Scanning"));
    m_scanningAction->setCheckable(true);
    m_scanningAction->setChecked(m_scanning);
    connect(m_scanningAction, &QAction::triggered, this, &TrayIcon::toggleScanningRequested);
    m_lookupWindowAction =
        m_menu->addAction(QIcon::fromTheme(QStringLiteral("edit-find")), i18nc("@action:inmenu", "Lookup Window"));
    m_lookupWindowAction->setCheckable(true);
    connect(m_lookupWindowAction, &QAction::triggered, this, &TrayIcon::lookupWindowRequested);

    m_menu->addSeparator();
    QAction *dictionaries = m_menu->addAction(QIcon::fromTheme(QStringLiteral("accessories-dictionary")),
                                              i18nc("@action:inmenu", "Manage Dictionaries…"));
    connect(dictionaries, &QAction::triggered, this, &TrayIcon::dictionariesRequested);
    QAction *settings =
        m_menu->addAction(QIcon::fromTheme(QStringLiteral("configure")), i18nc("@action:inmenu", "Configure MaruPop…"));
    connect(settings, &QAction::triggered, this, &TrayIcon::settingsRequested);
    QAction *about =
        m_menu->addAction(QIcon::fromTheme(QStringLiteral("help-about")), i18nc("@action:inmenu", "About MaruPop"));
    connect(about, &QAction::triggered, this, &TrayIcon::aboutRequested);

    m_menu->addSeparator();
    QAction *quit =
        m_menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), i18nc("@action:inmenu", "Quit"));
    connect(quit, &QAction::triggered, this, &TrayIcon::quitRequested);

    m_item->setContextMenu(m_menu);
    updateToolTip();

#ifdef Q_OS_WIN
    connect(m_item, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        // Qt reports a double click as Trigger followed by DoubleClick.
        if (reason == QSystemTrayIcon::Trigger) {
            Q_EMIT toggleScanningRequested();
        } else if (reason == QSystemTrayIcon::MiddleClick) {
            Q_EMIT lookupWindowRequested();
        }
    });
    // The taskbar mode is a Windows setting separate from the app mode that
    // QStyleHints::colorScheme() reports.
    connect(new win32::RegistryWatcher(QString{taskbarThemeKey}, this),
            &win32::RegistryWatcher::changed,
            this,
            &TrayIcon::updateIcon);
    m_hideAfterMessage = new QTimer(this);
    m_hideAfterMessage->setSingleShot(true);
    m_hideAfterMessage->setInterval(kMessageMs);
    connect(m_hideAfterMessage, &QTimer::timeout, this, [this] {
        m_item->setVisible(m_visible);
    });
#else
    // activateRequested is the primary click, which Plasma maps to a left click.
    connect(m_item, &KStatusNotifierItem::activateRequested, this, [this](bool /*active*/, const QPoint & /*pos*/) {
        Q_EMIT toggleScanningRequested();
    });
    // The middle click.
    connect(m_item, &KStatusNotifierItem::secondaryActivateRequested, this, &TrayIcon::lookupWindowRequested);
#endif
}

QMenu *TrayIcon::contextMenu() const
{
    return m_menu;
}

void TrayIcon::setVisible(bool visible)
{
#ifdef Q_OS_WIN
    m_visible = visible;
    m_hideAfterMessage->stop();
    m_item->setVisible(visible);
#else
    // KStatusNotifierItem has no hide: Passive is the status a host renders as absent.
    m_item->setStatus(visible ? KStatusNotifierItem::Active : KStatusNotifierItem::Passive);
#endif
}

void TrayIcon::setScanning(bool scanning)
{
    if (m_scanning == scanning) {
        return;
    }
    m_scanning = scanning;
    m_scanningAction->setChecked(scanning);
    updateIcon();
    updateToolTip();
}

void TrayIcon::updateIcon()
{
#ifdef Q_OS_WIN
    m_item->setIcon(applicationIcon(!m_scanning, IconBackground::Tray));
#else
    m_item->setIconByName(applicationIconName(!m_scanning));
#endif
}

#ifdef Q_OS_WIN
void TrayIcon::showMessage(const QString &title, const QString &text, bool failure)
{
    // QSystemTrayIcon shows a message only from a visible icon.
    if (!m_visible) {
        m_item->setVisible(true);
        m_hideAfterMessage->start();
    }
    m_item->showMessage(title, text, failure ? QSystemTrayIcon::Warning : QSystemTrayIcon::Information, kMessageMs);
}
#endif

void TrayIcon::setLookupWindowVisible(bool visible)
{
    m_lookupWindowAction->setChecked(visible);
}

void TrayIcon::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message) {
        return;
    }
    m_statusMessage = message;
    updateToolTip();
}

void TrayIcon::setStatusText(const QString &status)
{
    if (m_statusText == status) {
        return;
    }
    m_statusText = status;
    updateToolTip();
}

void TrayIcon::updateToolTip()
{
    // Three sources, most specific first: the last failure while one is set, then the scan
    // controller's own status line, then the bare scanning state. A failure is cleared by
    // passing an empty message, which uncovers whatever the controller last reported.
    QString subtitle = m_statusMessage;
    if (subtitle.isEmpty()) {
        subtitle = m_statusText;
    }
    if (subtitle.isEmpty()) {
        subtitle = m_scanning ? i18nc("@info:tooltip scanning state", "Scanning")
                              : i18nc("@info:tooltip scanning state", "Paused");
    }
#ifdef Q_OS_WIN
    // QSystemTrayIcon takes a plain-text tooltip. The notification area truncates a tooltip at
    // 127 characters.
    m_item->setToolTip(i18n("MaruPop") + QLatin1Char('\n') + subtitle);
#else
    m_item->setToolTip(applicationIconName(!m_scanning), i18n("MaruPop"), subtitle);
#endif
}

} // namespace maru
