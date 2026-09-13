// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#pragma once

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QString>

#include <KConfigDialog>

class KColorButton;
class QCheckBox;
class QComboBox;
class QSlider;
class QWidget;

namespace maru::popup
{
class PopupPreview;
}

namespace maru
{

class ShortcutButton;
class ShortcutRegistry;

// The settings window. Every entry of marupoprc binds itself through the kcfg_<Name>
// convention, which KConfigDialogManager resolves by object name, so six of the seven pages
// override none of updateSettings(), updateWidgets() or hasChanged().
//
// The seventh is Shortcuts. The three hotkeys are KGlobalAccel actions rather than kcfg
// entries -- so System Settings edits the same bindings the application registers -- and the
// config manager knows nothing about them, which is what the three overrides below are for.
class SettingsDialog : public KConfigDialog
{
    Q_OBJECT

public:
    // hotkeys may be null, which is what a test constructing the dialog without a live
    // KGlobalAccel registration passes. The Shortcuts page then edits its own copy and writes
    // it nowhere.
    explicit SettingsDialog(ShortcutRegistry *hotkeys = nullptr, QWidget *parent = nullptr);

Q_SIGNALS:
    // The "Manage Dictionaries…" button of the Lookup page. Application owns the dictionary
    // manager and the window over it, so the dialog only asks.
    void manageDictionariesRequested();
    // The model table on the Text Recognition page finished a download. Application owns the
    // notification channels, so the dialog only reports it.
    void modelsDownloaded();

protected:
    void updateSettings() override;       // the Shortcuts page -> KGlobalAccel
    void updateWidgets() override;        // KGlobalAccel -> the Shortcuts page
    void updateWidgetsDefault() override; // the Defaults button
    [[nodiscard]] bool hasChanged() override;

private:
    [[nodiscard]] QWidget *createGeneralPage();
    [[nodiscard]] QWidget *createScanningPage();
    [[nodiscard]] QWidget *createRecognitionPage();
    [[nodiscard]] QWidget *createLookupPage();
    [[nodiscard]] QWidget *createContentPage();
    [[nodiscard]] QWidget *createAppearancePage();
    [[nodiscard]] QWidget *createShortcutsPage();
    // The Shortcuts page for a session whose compositor owns the key sequence, which is what
    // ShortcutRegistry::editableShortcuts() returning false names. Read-only: the three
    // configuration lines, a copy button and the registration state.
    [[nodiscard]] QWidget *createCompositorShortcutsPage();

    void showShortcuts();
    // Moves the preset combo to Custom where the four colors and the opacity no longer match
    // the preset it names. Connected to the five widgets with a queued connection, so the
    // config manager loading them one at a time is judged on the state it leaves rather than
    // on each intermediate one.
    void syncPresetToColors();
    // Redraws the live popup preview from the widgets rather than from marupoprc: the values
    // the user is editing have not been written through yet, and writing them would leave the
    // config manager comparing every widget against itself and greying out Apply.
    void refreshPreview();

    ShortcutRegistry *m_hotkeys = nullptr;

    // The four color buttons and the preset combo of the Popup Appearance page. Picking a
    // preset fills the buttons; editing a button by hand moves the combo to Custom.
    QComboBox *m_themePreset = nullptr;
    QList<KColorButton *> m_colorButtons;
    QSlider *m_opacity = nullptr;
    bool m_fillingPreset = false;

    QWidget *m_contentPage = nullptr;
    QWidget *m_appearancePage = nullptr;
    popup::PopupPreview *m_preview = nullptr;
    QCheckBox *m_showKanji = nullptr;
    QList<QWidget *> m_kanjiOptions;

    // The Shortcuts page. Edited holds what the buttons show, stored what the registry holds;
    // Apply is the only thing that moves one onto the other.
    QHash<QString, ShortcutButton *> m_shortcutButtons;
    QHash<QString, QList<QKeySequence>> m_storedShortcuts;
    QHash<QString, QList<QKeySequence>> m_editedShortcuts;
};

} // namespace maru
