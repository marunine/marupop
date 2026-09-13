// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "app/trayicon.h"

#include <QAction>
#include <QIcon>
#include <QMenu>

#include <KLocalizedString>
#include <KStatusNotifierItem>

namespace maru
{

namespace
{

// The icon shown while scanning is off. Both names are installed by icons/CMakeLists.txt, in
// the same five sizes, so the swap resolves in the hicolor theme rather than falling back to
// the generic missing-icon glyph.
constexpr QLatin1StringView pausedIconName(MARUPOP_APPLICATION_ID "-paused");

} // namespace

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent)
    , m_item(new KStatusNotifierItem(QStringLiteral("marupop"), this))
    , m_menu(new QMenu)
{
    m_item->setCategory(KStatusNotifierItem::ApplicationStatus);
    // Active in both scanning states. NeedsAttention is for a condition the user has to act
    // on, and a scanning toggle the user set is not one.
    m_item->setStatus(KStatusNotifierItem::Active);
    m_item->setTitle(i18n("MaruPop"));
    m_item->setIconByName(pausedIconName);
    // The standard actions add a Quit entry that would bypass our own teardown.
    m_item->setStandardActionsEnabled(false);

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

    // activateRequested is the primary click, which Plasma maps to a left click.
    connect(m_item, &KStatusNotifierItem::activateRequested, this, [this](bool /*active*/, const QPoint & /*pos*/) {
        Q_EMIT toggleScanningRequested();
    });
    // The middle click.
    connect(m_item, &KStatusNotifierItem::secondaryActivateRequested, this, &TrayIcon::lookupWindowRequested);
}

QMenu *TrayIcon::contextMenu() const
{
    return m_menu;
}

void TrayIcon::setVisible(bool visible)
{
    // KStatusNotifierItem has no hide: Passive is the status a host renders as absent.
    m_item->setStatus(visible ? KStatusNotifierItem::Active : KStatusNotifierItem::Passive);
}

void TrayIcon::setScanning(bool scanning)
{
    if (m_scanning == scanning) {
        return;
    }
    m_scanning = scanning;
    m_scanningAction->setChecked(scanning);
    m_item->setIconByName(scanning ? QLatin1StringView(MARUPOP_APPLICATION_ID) : pausedIconName);
    updateToolTip();
}

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
    m_item->setToolTip(
        m_scanning ? QLatin1StringView(MARUPOP_APPLICATION_ID) : pausedIconName, i18n("MaruPop"), subtitle);
}

} // namespace maru
