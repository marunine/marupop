// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "app/shortcutregistry.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeySequence>

#include <KLocalizedString>

#include <array>

namespace maru
{

namespace
{

constexpr std::array kActionIds{
    QLatin1StringView("toggle-scanning"),
    QLatin1StringView("copy-word"),
    QLatin1StringView("pin-popup"),
};

} // namespace

ShortcutRegistry::ShortcutRegistry(QObject *parent)
    : QObject(parent)
{}

ShortcutRegistry::~ShortcutRegistry() = default;

QStringList ShortcutRegistry::actionIds()
{
    QStringList ids;
    ids.reserve(static_cast<qsizetype>(kActionIds.size()));
    for (const QLatin1StringView id : kActionIds) {
        ids.append(QString{id});
    }
    return ids;
}

QString ShortcutRegistry::actionLabel(const QString &id)
{
    if (id == QLatin1StringView("toggle-scanning")) {
        return i18nc("@action global shortcut", "Toggle Scanning");
    }
    if (id == QLatin1StringView("copy-word")) {
        return i18nc("@action global shortcut", "Copy Word");
    }
    if (id == QLatin1StringView("pin-popup")) {
        return i18nc("@action global shortcut", "Pin Popup");
    }
    return id;
}

QList<QKeySequence> ShortcutRegistry::defaultShortcut(const QString &id)
{
    // Default shortcuts use Meta+Alt. Users can change them to avoid session conflicts.
    if (id == QLatin1StringView("toggle-scanning")) {
        return {QKeySequence{Qt::META | Qt::ALT | Qt::Key_J}};
    }
    if (id == QLatin1StringView("copy-word")) {
        return {QKeySequence{Qt::META | Qt::ALT | Qt::Key_C}};
    }
    if (id == QLatin1StringView("pin-popup")) {
        return {QKeySequence{Qt::META | Qt::ALT | Qt::Key_P}};
    }
    return {};
}

QString ShortcutRegistry::componentName()
{
    // Deliberately not the desktop *file* name. kglobalacceld branches on the suffix in
    // GlobalShortcutsRegistry::getOrCreateComponent(): a component whose name ends in
    // ".desktop" becomes a KServiceActionComponent, which answers a key press by launching a
    // [Desktop Action] group of that entry through KIO instead of signalling the process that
    // registered the action. Our action names are ids no desktop action declares, so every
    // press would die inside the daemon. A bare name gets a plain Component, which is the one
    // that emits globalShortcutPressed back to our QAction. The shortcuts KCM still resolves
    // the entry for its name and icon: KService::serviceByStorageId() falls back to the
    // desktop base name.
    //
    // The same string is the app_id WlrShortcuts registers under, which is what a
    // `global` key binding in the compositor's own configuration names.
    const QString desktopName = QGuiApplication::desktopFileName();
    const QString name = desktopName.isEmpty() ? QCoreApplication::applicationName() : desktopName;
    // The suffix is dropped here rather than left to the caller.
    // QGuiApplication::setDesktopFileName() chops a trailing ".desktop" only when
    // QStandardPaths::locate(ApplicationsLocation) resolves the entry, so a build running
    // before its `make install`, or under a prefix outside XDG_DATA_DIRS, stores whatever
    // spelling it was handed.
    constexpr QLatin1StringView desktopSuffix{".desktop"};
    return name.endsWith(desktopSuffix) ? name.chopped(desktopSuffix.size()) : name;
}

} // namespace maru
