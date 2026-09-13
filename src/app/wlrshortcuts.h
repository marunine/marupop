// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// ShortcutRegistry over hyprland_global_shortcuts_v1.
//
// register_shortcut takes an id, an app_id, a description and a trigger_description, and
// hyprland_global_shortcut_v1 answers with pressed and released. The key sequence belongs to the
// compositor: the protocol states that "a global shortcut is anonymous, meaning the app does not
// know what key(s) trigger it"
// (third_party/protocols/hyprland-global-shortcuts-v1.xml, register_shortcut). editableShortcuts()
// is therefore false and bindingHint() answers the key binding for the user's own hyprland.lua
// or hyprland.conf, whichever capture::hyprlandConfig() finds.
//
// triggered() is emitted from pressed rather than from released, which is where KGlobalAccel
// emits its QAction::triggered as well.
#pragma once

#include "app/shortcutregistry.h"

#include <QHash>
#include <QString>

struct hyprland_global_shortcut_v1;
struct hyprland_global_shortcuts_manager_v1;

namespace maru
{

class WlrShortcuts : public ShortcutRegistry
{
    Q_OBJECT

public:
    explicit WlrShortcuts(QObject *parent = nullptr);
    ~WlrShortcuts() override;

    void registerActions() override;

    [[nodiscard]] bool isRegistered(const QString &id) const override;
    // False: the compositor owns the key sequence.
    [[nodiscard]] bool editableShortcuts() const override;
    // Always empty, because the compositor answers no key sequence for a registered shortcut.
    [[nodiscard]] QList<QKeySequence> shortcut(const QString &id) const override;
    // Always false, for the same reason.
    bool setShortcut(const QString &id, const QList<QKeySequence> &keys) override;

    // "bind = SUPER ALT, J, global, io.github.marunine.marupop:toggle-scanning", built from
    // ShortcutRegistry::defaultShortcut(id) so the suggested key matches the KDE default.
    [[nodiscard]] QString bindingHint(const QString &id) const override;
    [[nodiscard]] QString bindingHintHeader() const override;

    // True where hyprland_global_shortcuts_manager_v1 was bound.
    [[nodiscard]] bool isAvailable() const;
    // Empty while isAvailable() is true.
    [[nodiscard]] QString unavailableReason() const;

    // True where the compositor advertises hyprland_global_shortcuts_manager_v1, without binding
    // it.
    [[nodiscard]] static bool available();

    // The Hyprland dispatcher argument for id: "<app_id>:<id>". Pure, so the settings page and a
    // test build the same string.
    [[nodiscard]] static QString shortcutSelector(const QString &id);
    // The modifiers followed by the key, each in Hyprland's own spelling:
    // {"SUPER", "ALT", "J"}. Empty for an empty list. Tokens rather than one joined string
    // because the two configuration languages join them differently -- hyprlang separates the key
    // from the modifiers with a comma, Lua writes one plus-separated string -- so
    // capture::globalShortcutBind() is the only place that knows how, and nothing should join
    // these by hand.
    [[nodiscard]] static QStringList hyprlandKeyTokens(const QList<QKeySequence> &keys);

private:
    void onPressed(const QString &id);

    hyprland_global_shortcuts_manager_v1 *m_manager = nullptr;
    QString m_reason;
    QHash<QString, hyprland_global_shortcut_v1 *> m_shortcuts;

    // The listener's user data is one of these, so a press names its action without a reverse
    // lookup over m_shortcuts.
    struct Binding
    {
        WlrShortcuts *owner = nullptr;
        QString id;
    };

    QHash<QString, Binding *> m_bindings;

    friend struct GlobalShortcutListener;
};

} // namespace maru
