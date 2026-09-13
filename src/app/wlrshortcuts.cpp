// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "app/wlrshortcuts.h"

#include "capture/hyprlandconfig.h"
#include "core/logging.h"
#include "wayland-hyprland-global-shortcuts-v1-client-protocol.h"
#include "wayland/registry.h"

#include <QKeySequence>

#include <KLocalizedString>

namespace maru
{

namespace
{

constexpr QByteArrayView kManagerInterface("hyprland_global_shortcuts_manager_v1");

// Hyprland's spelling of the four modifiers a marupop default uses. SUPER is the Meta key,
// which Qt spells Qt::MetaModifier.
QStringList hyprlandModifiers(Qt::KeyboardModifiers modifiers)
{
    QStringList names;
    if (modifiers.testFlag(Qt::MetaModifier)) {
        names.append(QStringLiteral("SUPER"));
    }
    if (modifiers.testFlag(Qt::ControlModifier)) {
        names.append(QStringLiteral("CTRL"));
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        names.append(QStringLiteral("ALT"));
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        names.append(QStringLiteral("SHIFT"));
    }
    return names;
}

} // namespace

// The two hyprland_global_shortcut_v1 events. The user data is a WlrShortcuts::Binding, which
// carries the action id, so a press needs no reverse lookup.
struct GlobalShortcutListener
{
    static void pressed(
        void *data, hyprland_global_shortcut_v1 * /*shortcut*/, quint32 /*secHi*/, quint32 /*secLo*/, quint32 /*nsec*/)
    {
        auto *binding = static_cast<WlrShortcuts::Binding *>(data);
        binding->owner->onPressed(binding->id);
    }

    static void released(void * /*data*/,
                         hyprland_global_shortcut_v1 * /*shortcut*/,
                         quint32 /*secHi*/,
                         quint32 /*secLo*/,
                         quint32 /*nsec*/)
    {
        // The three actions are all edge-triggered on the press, which is where KGlobalAccel
        // emits QAction::triggered as well.
    }
};

namespace
{

const hyprland_global_shortcut_v1_listener kShortcutListener = {
    .pressed = &GlobalShortcutListener::pressed,
    .released = &GlobalShortcutListener::released,
};

} // namespace

bool WlrShortcuts::available()
{
    wl::Registry *registry = wl::Registry::instance();
    return registry != nullptr && registry->has(kManagerInterface.toByteArray());
}

WlrShortcuts::WlrShortcuts(QObject *parent)
    : ShortcutRegistry(parent)
{
    wl::Registry *registry = wl::Registry::instance();
    if (registry == nullptr) {
        m_reason = i18n("The application is not connected to a Wayland compositor.");
        return;
    }
    m_manager = static_cast<hyprland_global_shortcuts_manager_v1 *>(
        registry->bind(&hyprland_global_shortcuts_manager_v1_interface, 1));
    if (m_manager == nullptr) {
        m_reason =
            i18n("Global shortcuts unavailable: the compositor does not support hyprland_global_shortcuts_manager_v1.");
    }
}

WlrShortcuts::~WlrShortcuts()
{
    for (hyprland_global_shortcut_v1 *shortcut : std::as_const(m_shortcuts)) {
        hyprland_global_shortcut_v1_destroy(shortcut);
    }
    qDeleteAll(m_bindings);
    if (m_manager != nullptr) {
        hyprland_global_shortcuts_manager_v1_destroy(m_manager);
    }
}

void WlrShortcuts::registerActions()
{
    if (m_manager == nullptr) {
        qCWarning(logApp) << "no global shortcut manager;" << m_reason;
        return;
    }
    const QStringList ids = actionIds();
    for (const QString &id : ids) {
        if (m_shortcuts.contains(id)) {
            continue;
        }
        auto *binding = new Binding{.owner = this, .id = id};
        // The compositor raises already_taken for a duplicate app_id and id pair, which is what
        // a second marupop process would hit. The protocol error kills the connection, so the
        // guard above is what keeps one process from registering twice.
        hyprland_global_shortcut_v1 *shortcut =
            hyprland_global_shortcuts_manager_v1_register_shortcut(m_manager,
                                                                   id.toUtf8().constData(),
                                                                   componentName().toUtf8().constData(),
                                                                   actionLabel(id).toUtf8().constData(),
                                                                   bindingHint(id).toUtf8().constData());
        hyprland_global_shortcut_v1_add_listener(shortcut, &kShortcutListener, binding);
        m_shortcuts.insert(id, shortcut);
        m_bindings.insert(id, binding);
    }
    if (wl::Registry *registry = wl::Registry::instance(); registry != nullptr) {
        // A protocol error on a duplicate registration arrives as a fatal display error rather
        // than as a return value, so the roundtrip is what turns it into a diagnostic at startup
        // instead of at the first key press.
        registry->roundtrip();
    }
    qCDebug(logApp) << "registered" << m_shortcuts.size() << "global shortcuts with the compositor";
}

bool WlrShortcuts::isRegistered(const QString &id) const
{
    return m_shortcuts.contains(id);
}

bool WlrShortcuts::editableShortcuts() const
{
    return false;
}

QList<QKeySequence> WlrShortcuts::shortcut(const QString & /*id*/) const
{
    return {};
}

bool WlrShortcuts::setShortcut(const QString &id, const QList<QKeySequence> & /*keys*/)
{
    qCDebug(logApp) << "the compositor owns the key sequence for" << id;
    return false;
}

QString WlrShortcuts::shortcutSelector(const QString &id)
{
    return componentName() + QLatin1Char(':') + id;
}

QStringList WlrShortcuts::hyprlandKeyTokens(const QList<QKeySequence> &keys)
{
    if (keys.isEmpty() || keys.first().isEmpty()) {
        return {};
    }
    const QKeyCombination combination = keys.first()[0];
    QStringList tokens = hyprlandModifiers(combination.keyboardModifiers());
    // QKeySequence::toString() of the key alone, which is the spelling Hyprland's key parser
    // takes for a printable key and for a named one such as "F1".
    tokens.append(QKeySequence(combination.key()).toString(QKeySequence::PortableText));
    return tokens;
}

QString WlrShortcuts::bindingHint(const QString &id) const
{
    // Written in whichever of Hyprland's two configuration languages this user's own
    // configuration is in, the same way the capture report's two lines are.
    return capture::globalShortcutBind(
        capture::hyprlandConfig().language, hyprlandKeyTokens(defaultShortcut(id)), shortcutSelector(id));
}

QString WlrShortcuts::bindingHintHeader() const
{
    return i18n("To configure global shortcuts, add these lines to %1 and reload the configuration:",
                capture::hyprlandConfig().fileName());
}

bool WlrShortcuts::isAvailable() const
{
    return m_manager != nullptr;
}

QString WlrShortcuts::unavailableReason() const
{
    return m_manager != nullptr ? QString{} : m_reason;
}

void WlrShortcuts::onPressed(const QString &id)
{
    Q_EMIT triggered(id);
}

} // namespace maru
