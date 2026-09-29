// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// RegisterHotKey binds one modifier set and one virtual key to a window. The first process to
// register a combination holds the combination. A later registration fails with
// ERROR_HOTKEY_ALREADY_REGISTERED (1409). Game Bar holds Win+Alt+G and Win+Alt+R. The Windows shell
// holds Win with a letter for its own shortcuts, for example Win+E, Win+L and Win+R. A
// RegisterHotKey registration ends with the registering window.
// marupoprc stores the key sequences under [Shortcuts], keyed by action id, in
// QKeySequence::PortableText format.
#pragma once

#include "app/shortcutregistry.h"

#include <QHash>
#include <QKeySequence>
#include <QSet>

#include <memory>
#include <optional>

namespace maru::win32
{
class MessageWindow;
}

namespace maru
{

// The RegisterHotKey arguments for one key combination: MOD_* flags and a virtual-key code.
struct WinHotKey
{
    unsigned modifiers = 0;
    unsigned virtualKey = 0;

    friend bool operator==(const WinHotKey &, const WinHotKey &) = default;
};

// The RegisterHotKey arguments for the first combination of sequence, excluding MOD_NOREPEAT.
// Returns nullopt for an empty sequence, for a key without a virtual-key code and for a
// combination without a modifier. RegisterHotKey accepts a bare key. A bare letter registered
// globally reaches the registering window alone. Punctuation maps through the active keyboard
// layout.
[[nodiscard]] std::optional<WinHotKey> winHotKeyFor(const QKeySequence &sequence);

class WinShortcuts : public ShortcutRegistry
{
    Q_OBJECT

public:
    explicit WinShortcuts(QObject *parent = nullptr);
    ~WinShortcuts() override;

    // Registers the stored sequence of every action, or the default sequence of an action
    // without a stored one. A repeated call keeps an unchanged registered sequence and retries a
    // sequence that another application held.
    void registerActions() override;

    // True where RegisterHotKey accepted the action's sequence.
    [[nodiscard]] bool isRegistered(const QString &id) const override;
    // True: MaruPop stores and registers the sequences.
    [[nodiscard]] bool editableShortcuts() const override;
    [[nodiscard]] QList<QKeySequence> shortcut(const QString &id) const override;
    // Registers keys in place of the current sequence of the action and stores them. Returns
    // false for an unknown id and for a sequence that RegisterHotKey refuses. On false, the
    // previous sequence stays registered and stored. An empty list removes the binding.
    bool setShortcut(const QString &id, const QList<QKeySequence> &keys) override;

    [[nodiscard]] QString bindingHint(const QString &id) const override;
    [[nodiscard]] QString bindingHintHeader() const override;

    // Emits triggered() for the action registered under hotKeyId, the WPARAM of a WM_HOTKEY.
    // Public so that tests call handleHotKey() in place of a global key press.
    void handleHotKey(int hotKeyId);

private:
    bool registerKeys(const QString &id, const QList<QKeySequence> &keys);
    void unregister(const QString &id);
    [[nodiscard]] static QList<QKeySequence> storedShortcut(const QString &id);
    static void storeShortcut(const QString &id, const QList<QKeySequence> &keys);

    std::unique_ptr<win32::MessageWindow> m_window;
    QHash<QString, QList<QKeySequence>> m_shortcuts;
    QSet<QString> m_registered;
};

} // namespace maru
