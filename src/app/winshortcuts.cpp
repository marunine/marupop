// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "app/winshortcuts.h"

#include "core/logging.h"
#include "core/settings.h"
#include "win32/messagewindow.h"

#include <KConfigGroup>

#include <array>
#include <utility>
#include <windows.h>

namespace maru
{

namespace
{

constexpr QLatin1StringView kGroup{"Shortcuts"};

KConfigGroup shortcutsGroup()
{
    return PopSettings::self()->sharedConfig()->group(QString{kGroup});
}

// RegisterHotKey identifiers are per window and must lie in 0x0000-0xBFFF for an application.
int hotKeyIdOf(const QString &id)
{
    return static_cast<int>(ShortcutRegistry::actionIds().indexOf(id)) + 1;
}

constexpr std::array<std::pair<int, unsigned>, 30> kNamedKeys{{
    {Qt::Key_Space, VK_SPACE},
    {Qt::Key_Tab, VK_TAB},
    {Qt::Key_Return, VK_RETURN},
    {Qt::Key_Enter, VK_RETURN},
    {Qt::Key_Escape, VK_ESCAPE},
    {Qt::Key_Backspace, VK_BACK},
    {Qt::Key_Insert, VK_INSERT},
    {Qt::Key_Delete, VK_DELETE},
    {Qt::Key_Home, VK_HOME},
    {Qt::Key_End, VK_END},
    {Qt::Key_PageUp, VK_PRIOR},
    {Qt::Key_PageDown, VK_NEXT},
    {Qt::Key_Left, VK_LEFT},
    {Qt::Key_Right, VK_RIGHT},
    {Qt::Key_Up, VK_UP},
    {Qt::Key_Down, VK_DOWN},
    {Qt::Key_Pause, VK_PAUSE},
    {Qt::Key_Print, VK_SNAPSHOT},
    {Qt::Key_ScrollLock, VK_SCROLL},
    {Qt::Key_CapsLock, VK_CAPITAL},
    {Qt::Key_NumLock, VK_NUMLOCK},
    {Qt::Key_Menu, VK_APPS},
    {Qt::Key_Plus, VK_OEM_PLUS},
    {Qt::Key_Minus, VK_OEM_MINUS},
    {Qt::Key_Comma, VK_OEM_COMMA},
    {Qt::Key_Period, VK_OEM_PERIOD},
    {Qt::Key_VolumeUp, VK_VOLUME_UP},
    {Qt::Key_VolumeDown, VK_VOLUME_DOWN},
    {Qt::Key_VolumeMute, VK_VOLUME_MUTE},
    {Qt::Key_MediaPlay, VK_MEDIA_PLAY_PAUSE},
}};

// Qt::Key values of letters and digits equal their ASCII codes, which equal their virtual-key
// codes.
std::optional<unsigned> virtualKeyFor(Qt::Key key)
{
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9)) {
        return static_cast<unsigned>(key);
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return static_cast<unsigned>(VK_F1 + (key - Qt::Key_F1));
    }
    for (const auto &[qtKey, virtualKey] : kNamedKeys) {
        if (qtKey == key) {
            return virtualKey;
        }
    }
    // VkKeyScanW reports the Shift state in the high byte. The high byte is dropped because the
    // recorded sequence carries its own modifiers.
    if (key > 0x20 && key < 0x7f) {
        const SHORT scan = VkKeyScanW(static_cast<WCHAR>(key));
        if (scan != -1 && LOBYTE(scan) != 0xff) {
            return static_cast<unsigned>(LOBYTE(scan));
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<WinHotKey> winHotKeyFor(const QKeySequence &sequence)
{
    if (sequence.isEmpty()) {
        return std::nullopt;
    }
    const QKeyCombination combination = sequence[0];
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    WinHotKey hotKey;
    if (modifiers.testFlag(Qt::MetaModifier)) {
        hotKey.modifiers |= MOD_WIN;
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        hotKey.modifiers |= MOD_ALT;
    }
    if (modifiers.testFlag(Qt::ControlModifier)) {
        hotKey.modifiers |= MOD_CONTROL;
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        hotKey.modifiers |= MOD_SHIFT;
    }
    if (hotKey.modifiers == 0) {
        return std::nullopt;
    }
    const std::optional<unsigned> virtualKey = virtualKeyFor(combination.key());
    if (!virtualKey.has_value()) {
        return std::nullopt;
    }
    hotKey.virtualKey = *virtualKey;
    return hotKey;
}

WinShortcuts::WinShortcuts(QObject *parent)
    : ShortcutRegistry(parent)
    , m_window(std::make_unique<win32::MessageWindow>([this](unsigned message, quintptr wParam, qintptr) {
        if (message != WM_HOTKEY) {
            return false;
        }
        handleHotKey(static_cast<int>(wParam));
        return true;
    }))
{}

WinShortcuts::~WinShortcuts()
{
    const QStringList ids = actionIds();
    for (const QString &id : ids) {
        unregister(id);
    }
}

void WinShortcuts::registerActions()
{
    const QStringList ids = actionIds();
    for (const QString &id : ids) {
        const QList<QKeySequence> keys = storedShortcut(id);
        if (m_shortcuts.contains(id) && m_shortcuts.value(id) == keys && (keys.isEmpty() || isRegistered(id))) {
            continue;
        }
        m_shortcuts.insert(id, keys);
        if (!registerKeys(id, keys) && !keys.isEmpty()) {
            qCWarning(logApp) << "could not register the global shortcut" << keys.constFirst().toString() << "for" << id
                              << "; another application holds it";
        }
    }
}

bool WinShortcuts::isRegistered(const QString &id) const
{
    return m_registered.contains(id);
}

bool WinShortcuts::editableShortcuts() const
{
    return true;
}

QList<QKeySequence> WinShortcuts::shortcut(const QString &id) const
{
    return m_shortcuts.value(id);
}

bool WinShortcuts::setShortcut(const QString &id, const QList<QKeySequence> &keys)
{
    if (!actionIds().contains(id)) {
        qCWarning(logApp) << "no registered action named" << id;
        return false;
    }
    if (!registerKeys(id, keys)) {
        // registerKeys() unregistered the previous sequence before the failed registration.
        registerKeys(id, m_shortcuts.value(id));
        return false;
    }
    m_shortcuts.insert(id, keys);
    storeShortcut(id, keys);
    return true;
}

QString WinShortcuts::bindingHint(const QString & /*id*/) const
{
    return {};
}

QString WinShortcuts::bindingHintHeader() const
{
    return {};
}

void WinShortcuts::handleHotKey(int hotKeyId)
{
    const QStringList ids = actionIds();
    const qsizetype index = hotKeyId - 1;
    if (index < 0 || index >= ids.size()) {
        return;
    }
    Q_EMIT triggered(ids.at(index));
}

bool WinShortcuts::registerKeys(const QString &id, const QList<QKeySequence> &keys)
{
    unregister(id);
    if (keys.isEmpty()) {
        return true;
    }
    const std::optional<WinHotKey> hotKey = winHotKeyFor(keys.constFirst());
    auto *window = static_cast<HWND>(m_window->handle());
    if (!hotKey.has_value() || window == nullptr) {
        return false;
    }
    // MOD_NOREPEAT: holding the keys down triggers the action once per press.
    if (RegisterHotKey(window, hotKeyIdOf(id), hotKey->modifiers | MOD_NOREPEAT, hotKey->virtualKey) == FALSE) {
        qCDebug(logApp) << "RegisterHotKey refused" << keys.constFirst().toString() << "for" << id << "with error"
                        << GetLastError();
        return false;
    }
    m_registered.insert(id);
    return true;
}

void WinShortcuts::unregister(const QString &id)
{
    if (!m_registered.remove(id)) {
        return;
    }
    UnregisterHotKey(static_cast<HWND>(m_window->handle()), hotKeyIdOf(id));
}

QList<QKeySequence> WinShortcuts::storedShortcut(const QString &id)
{
    const KConfigGroup group = shortcutsGroup();
    if (!group.hasKey(id)) {
        return defaultShortcut(id);
    }
    const QString stored = group.readEntry(id, QString{});
    if (stored.isEmpty()) {
        return {};
    }
    return {QKeySequence::fromString(stored, QKeySequence::PortableText)};
}

void WinShortcuts::storeShortcut(const QString &id, const QList<QKeySequence> &keys)
{
    KConfigGroup group = shortcutsGroup();
    group.writeEntry(id, keys.isEmpty() ? QString{} : keys.constFirst().toString(QKeySequence::PortableText));
    group.sync();
}

} // namespace maru
