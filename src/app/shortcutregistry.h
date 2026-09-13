// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The three global shortcuts, behind the one interface both session families answer.
//
// The two implementations differ in who owns the key sequence. KGlobalAccel stores it in
// kglobalshortcutsrc and lets the application write it, which is what the Shortcuts settings page
// edits. hyprland_global_shortcuts_v1 registers an anonymous action and states that "a global
// shortcut is anonymous, meaning the app does not know what key(s) trigger it"
// (third_party/protocols/hyprland-global-shortcuts-v1.xml), so the key sequence is a key binding
// in the compositor's own configuration. editableShortcuts() is what the settings page reads to
// decide which of the two pages to build, and bindingHint() is the line the second one shows.
#pragma once

#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace maru
{

class ShortcutRegistry : public QObject
{
    Q_OBJECT

public:
    explicit ShortcutRegistry(QObject *parent = nullptr);
    ~ShortcutRegistry() override;

    // Stable action IDs in settings-page order: toggle-scanning, copy-word, pin-popup.
    [[nodiscard]] static QStringList actionIds();
    // The label the shortcuts page and, on KDE, the System Settings row show.
    [[nodiscard]] static QString actionLabel(const QString &id);
    // The sequence an untouched installation binds. The wlroots
    // implementation reports it as the key to put in the `bind` line rather than binding it.
    [[nodiscard]] static QList<QKeySequence> defaultShortcut(const QString &id);
    // The application id, which names the group kglobalshortcutsrc puts the actions under and
    // the app_id hyprland_global_shortcuts_v1 registers them under.
    [[nodiscard]] static QString componentName();

    // Registers the three actions with whatever the session offers. Called once at startup.
    virtual void registerActions() = 0;

    // True where id reached the session's shortcut service.
    [[nodiscard]] virtual bool isRegistered(const QString &id) const = 0;

    // True where setShortcut() can change a key sequence, which is what decides whether the
    // Shortcuts settings page shows editors or the lines to paste.
    [[nodiscard]] virtual bool editableShortcuts() const = 0;

    // The keys bound to id right now, empty for an unbound or an unknown action and always empty
    // on an implementation whose editableShortcuts() is false.
    [[nodiscard]] virtual QList<QKeySequence> shortcut(const QString &id) const = 0;
    // Writes keys through to the session's shortcut service. False for an unknown id, for a
    // registration the service refused, and on an implementation whose editableShortcuts() is
    // false.
    virtual bool setShortcut(const QString &id, const QList<QKeySequence> &keys) = 0;

    // The compositor configuration line that binds id, for an implementation that cannot set the
    // key itself. Empty where editableShortcuts() is true.
    [[nodiscard]] virtual QString bindingHint(const QString &id) const = 0;
    // A sentence naming the file bindingHint() lines belong in. Empty where editableShortcuts()
    // is true.
    [[nodiscard]] virtual QString bindingHintHeader() const = 0;

Q_SIGNALS:
    void triggered(const QString &id);
};

} // namespace maru
