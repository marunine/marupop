// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "app/hotkeyregistry.h"

#include "core/logging.h"

#include <QAction>

#include <KGlobalAccel>
#include <KLocalizedString>

namespace maru
{

HotkeyRegistry::HotkeyRegistry(QObject *parent)
    : ShortcutRegistry(parent)
{}

// The registrations are left in place: KGlobalAccel keeps them so System Settings can still
// show and edit the bindings while the application is not running.
HotkeyRegistry::~HotkeyRegistry() = default;

void HotkeyRegistry::registerActions()
{
    const QStringList ids = actionIds();
    for (const QString &id : ids) {
        QAction *action = m_actions.value(id);
        if (action == nullptr) {
            action = new QAction(actionLabel(id), this);
            action->setObjectName(id);
            // KGlobalAccel reads both properties off the action itself; setting them keeps
            // this free of a KActionCollection, and so of KXmlGui.
            action->setProperty("componentName", componentName());
            action->setProperty("componentDisplayName", i18n("MaruPop"));
            connect(action, &QAction::triggered, this, [this, id] {
                Q_EMIT triggered(id);
            });
            m_actions.insert(id, action);
        }
        action->setText(actionLabel(id));
        KGlobalAccel::self()->setDefaultShortcut(action, defaultShortcut(id), KGlobalAccel::NoAutoloading);
        // Autoloading returns whatever the daemon already stores, which is what preserves a
        // binding the user changed in System Settings across a restart.
        if (!KGlobalAccel::self()->setShortcut(action, defaultShortcut(id), KGlobalAccel::Autoloading)) {
            qCWarning(logApp) << "could not register the global shortcut for" << id;
        }
        // The daemon is the authority once it answers; a session without one -- a test, or a
        // plain X session with no kglobalacceld -- leaves the default as what the shortcuts
        // page shows.
        const QList<QKeySequence> stored = KGlobalAccel::self()->shortcut(action);
        m_shortcuts.insert(id, stored.isEmpty() ? defaultShortcut(id) : stored);
    }
}

bool HotkeyRegistry::isRegistered(const QString &id) const
{
    return m_actions.contains(id);
}

bool HotkeyRegistry::editableShortcuts() const
{
    return true;
}

QList<QKeySequence> HotkeyRegistry::shortcut(const QString &id) const
{
    return m_shortcuts.value(id);
}

bool HotkeyRegistry::setShortcut(const QString &id, const QList<QKeySequence> &keys)
{
    QAction *action = m_actions.value(id);
    if (action == nullptr) {
        qCWarning(logApp) << "no registered action named" << id;
        return false;
    }
    // The bookkeeping is written whatever the daemon answers: the settings page reads it back
    // to draw the button, and a session with no kglobalacceld would otherwise show the old
    // sequence after the user recorded a new one.
    m_shortcuts.insert(id, keys);
    // NoAutoloading is the documented way to change a stored shortcut: Autoloading would
    // return what the daemon already holds and ignore what it was passed.
    return KGlobalAccel::self()->setShortcut(action, keys, KGlobalAccel::NoAutoloading);
}

QString HotkeyRegistry::bindingHint(const QString & /*id*/) const
{
    return {};
}

QString HotkeyRegistry::bindingHintHeader() const
{
    return {};
}

} // namespace maru
