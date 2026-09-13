// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#pragma once

#include "app/shortcutregistry.h"

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QString>

class QAction;

namespace maru
{

// ShortcutRegistry over KGlobalAccel, registered in-process. The application is tray-resident,
// so Spectacle's desktop-action indirection is unnecessary -- but that also means the component
// name must not look like a desktop entry, or kglobalacceld takes the indirection anyway and the
// presses never arrive (ShortcutRegistry::componentName() has the detail).
//
// The component name and the action object names together key kglobalshortcutsrc and the
// System Settings shortcuts page, so both have to stay stable across runs.
//
// kglobalacceld receives key events through KWin's KGlobalAccel integration, which no
// wlroots-family compositor carries, so this implementation reaches its daemon on a KDE Plasma
// session alone. WlrShortcuts is the one the other sessions build.
class HotkeyRegistry : public ShortcutRegistry
{
    Q_OBJECT

public:
    explicit HotkeyRegistry(QObject *parent = nullptr);
    ~HotkeyRegistry() override;

    // Registers one QAction per id with KGlobalAccel. Registration is Autoloading, so a
    // shortcut the user changed in System Settings wins over the default here.
    void registerActions() override;

    [[nodiscard]] bool isRegistered(const QString &id) const override;

    [[nodiscard]] bool editableShortcuts() const override;

    // The keys bound to id right now, empty for an unbound or an unknown action. Read from the
    // registry's own bookkeeping rather than from the daemon, so the answer is the one the
    // settings page last wrote even where no kglobalacceld is running.
    [[nodiscard]] QList<QKeySequence> shortcut(const QString &id) const override;
    // Writes keys through to the daemon with NoAutoloading, which is the documented way to
    // change a stored shortcut. False for an unknown id or a registration the daemon refused.
    bool setShortcut(const QString &id, const QList<QKeySequence> &keys) override;

    [[nodiscard]] QString bindingHint(const QString &id) const override;
    [[nodiscard]] QString bindingHintHeader() const override;

private:
    QHash<QString, QAction *> m_actions;
    QHash<QString, QList<QKeySequence>> m_shortcuts;
};

} // namespace maru
