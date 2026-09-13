// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The kcfg_ binding of the settings dialog. KConfigDialogManager finds a widget by its object
// name and writes it through on Apply, so what this covers is that each page names its widgets
// after the entries they edit: a renamed entry or a mistyped object name leaves a control the
// dialog shows and never writes.
//
// The Shortcuts page is the one page the manager knows nothing about -- the three hotkeys are
// KGlobalAccel actions rather than marupoprc entries -- so its own updateSettings() and
// hasChanged() are covered separately, over a dialog with no registry behind it.
#include "app/hotkeyregistry.h"
#include "app/settingsdialog.h"
#include "app/shortcutbutton.h"
#include "core/settings.h"
#include "popup/theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QKeySequence>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>

#include <KColorButton>
#include <KPageWidget>
#include <KPageWidgetModel>

#include <gtest/gtest.h>

using namespace maru;

namespace
{

// One dialog over the PopSettings singleton, built the way Application::showSettings() builds
// it, minus the hotkey registry: registering the three actions would reach the tester's own
// kglobalacceld, which no test may write to.
struct Fixture
{
    SettingsDialog *dialog = nullptr;

    Fixture()
        : dialog(new SettingsDialog)
    {}

    ~Fixture()
    {
        delete dialog;
    }

    Fixture(const Fixture &) = delete;
    Fixture &operator=(const Fixture &) = delete;

    template <typename T>
    [[nodiscard]] T *widget(const char *objectName) const
    {
        return dialog->findChild<T *>(QString::fromLatin1(objectName));
    }

    // KConfigDialog only writes through on Apply, exactly as a user would. The button starts
    // disabled until KConfigDialogManager detects a change, and a programmatic setChecked()
    // reaches it through the widget's own signal, so the enable is a belt on the same path.
    void apply() const
    {
        QPushButton *button = dialog->button(QDialogButtonBox::Apply);
        button->setEnabled(true);
        button->click();
    }

    [[nodiscard]] bool applyEnabled() const
    {
        return dialog->button(QDialogButtonBox::Apply)->isEnabled();
    }
};

void resetSettings()
{
    PopSettings::self()->setDefaults();
    PopSettings::self()->save();
}

} // namespace

TEST(SettingsDialogTest, buildsEveryPage)
{
    resetSettings();
    const Fixture fixture;
    // General, Scanning, Text Recognition, Lookup, Popup Content, Popup Appearance, Shortcuts.
    // KPageDialog keeps its page widget protected, so the page list is read off the view the
    // dialog builds rather than through the accessor.
    auto *pages = fixture.dialog->findChild<KPageWidget *>();
    ASSERT_NE(pages, nullptr);
    ASSERT_NE(pages->model(), nullptr);
    EXPECT_EQ(pages->model()->rowCount(), 7);
}

TEST(SettingsDialogTest, showsTheStoredValueOfEveryBoundCheckBox)
{
    resetSettings();
    PopSettings::setShowTrayIcon(false);
    PopSettings::setAutostart(true);
    PopSettings::self()->save();

    const Fixture fixture;
    auto *tray = fixture.widget<QCheckBox>("kcfg_ShowTrayIcon");
    ASSERT_NE(tray, nullptr);
    EXPECT_FALSE(tray->isChecked());
    auto *autostart = fixture.widget<QCheckBox>("kcfg_Autostart");
    ASSERT_NE(autostart, nullptr);
    EXPECT_TRUE(autostart->isChecked());
}

TEST(SettingsDialogTest, writesABoundCheckBoxThroughOnApply)
{
    resetSettings();
    ASSERT_TRUE(PopSettings::showTrayIcon());

    const Fixture fixture;
    auto *tray = fixture.widget<QCheckBox>("kcfg_ShowTrayIcon");
    ASSERT_NE(tray, nullptr);
    tray->setChecked(false);
    fixture.apply();

    EXPECT_FALSE(PopSettings::showTrayIcon());
}

// One bound widget from every group of marupopsettings.kcfg, written through the real Apply
// button. A group whose page never got built, or whose object names drifted, fails here.
TEST(SettingsDialogTest, writesOneWidgetOfEveryGroupThroughOnApply)
{
    resetSettings();
    const Fixture fixture;

    auto *notify = fixture.widget<QCheckBox>("kcfg_NotifyOnError");                   // General
    auto *throttle = fixture.widget<QSpinBox>("kcfg_CursorMoveThrottleMs");           // Scanning
    auto *threads = fixture.widget<QSpinBox>("kcfg_MeikiIntraOpThreads");             // Ocr
    auto *threshold = fixture.widget<QDoubleSpinBox>("kcfg_MeikiDetectionThreshold"); // Ocr
    auto *results = fixture.widget<QSpinBox>("kcfg_MaxResults");                      // Lookup
    auto *compact = fixture.widget<QCheckBox>("kcfg_CompactMode");                    // PopupContent
    auto *radius = fixture.widget<QSpinBox>("kcfg_PopupCornerRadius");                // PopupAppearance
    ASSERT_NE(notify, nullptr);
    ASSERT_NE(throttle, nullptr);
    ASSERT_NE(threads, nullptr);
    ASSERT_NE(threshold, nullptr);
    ASSERT_NE(results, nullptr);
    ASSERT_NE(compact, nullptr);
    ASSERT_NE(radius, nullptr);

    notify->setChecked(false);
    throttle->setValue(321);
    threads->setValue(3);
    threshold->setValue(0.75);
    results->setValue(17);
    compact->setChecked(false);
    radius->setValue(21);
    fixture.apply();

    EXPECT_FALSE(PopSettings::notifyOnError());
    EXPECT_EQ(PopSettings::cursorMoveThrottleMs(), 321);
    EXPECT_EQ(PopSettings::meikiIntraOpThreads(), 3);
    EXPECT_DOUBLE_EQ(PopSettings::meikiDetectionThreshold(), 0.75);
    EXPECT_EQ(PopSettings::maxResults(), 17);
    EXPECT_FALSE(PopSettings::compactMode());
    EXPECT_EQ(PopSettings::popupCornerRadius(), 21);
}

// An enum entry is bound by index, which is what the kcfg choice lists mirroring core/enums.h
// declaration order buys. A combo that lost an item, or gained one out of order, writes the
// wrong enumerator with no other symptom.
TEST(SettingsDialogTest, writesAnEnumComboByIndex)
{
    resetSettings();
    const Fixture fixture;
    auto *category = fixture.widget<QComboBox>("kcfg_LookupCategory");
    ASSERT_NE(category, nullptr);
    ASSERT_EQ(category->count(), 4);

    category->setCurrentIndex(static_cast<int>(LookupCategory::Name));
    fixture.apply();
    EXPECT_EQ(settings::lookupCategory(), LookupCategory::Name);
}

TEST(SettingsDialogTest, disablesTheKanjiSubOptionsWithTheCard)
{
    resetSettings();
    const Fixture fixture;
    auto *card = fixture.widget<QCheckBox>("kcfg_ShowKanji");
    auto *examples = fixture.widget<QCheckBox>("kcfg_ShowKanjiExamples");
    ASSERT_NE(card, nullptr);
    ASSERT_NE(examples, nullptr);
    EXPECT_TRUE(examples->isEnabled());

    card->setChecked(false);
    EXPECT_FALSE(examples->isEnabled());
    card->setChecked(true);
    EXPECT_TRUE(examples->isEnabled());
}

TEST(SettingsDialogTest, fillsTheColorButtonsFromTheSelectedPreset)
{
    resetSettings();
    const Fixture fixture;
    auto *preset = fixture.widget<QComboBox>("kcfg_ThemePreset");
    auto *background = fixture.widget<KColorButton>("kcfg_ColorBackground");
    auto *foreground = fixture.widget<KColorButton>("kcfg_ColorForeground");
    auto *opacity = fixture.widget<QSlider>("kcfg_BackgroundOpacity");
    ASSERT_NE(preset, nullptr);
    ASSERT_NE(background, nullptr);
    ASSERT_NE(foreground, nullptr);
    ASSERT_NE(opacity, nullptr);

    // activated() rather than setCurrentIndex(): the user picking an entry is what fills the
    // buttons, and the config manager setting the same index while loading must not.
    const int academic = static_cast<int>(ThemePreset::Academic);
    preset->setCurrentIndex(academic);
    Q_EMIT preset->activated(academic);

    const popup::Theme expected = popup::presetTheme(ThemePreset::Academic);
    EXPECT_EQ(background->color(), expected.background);
    EXPECT_EQ(foreground->color(), expected.foreground);
    EXPECT_EQ(opacity->value(), expected.backgroundOpacity);

    fixture.apply();
    EXPECT_EQ(settings::themePreset(), ThemePreset::Academic);
    EXPECT_EQ(PopSettings::colorBackground(), expected.background);
    EXPECT_EQ(PopSettings::colorForeground(), expected.foreground);
}

TEST(SettingsDialogTest, movesThePresetToCustomWhenAColorIsEditedByHand)
{
    resetSettings();
    const Fixture fixture;
    auto *preset = fixture.widget<QComboBox>("kcfg_ThemePreset");
    auto *headword = fixture.widget<KColorButton>("kcfg_ColorHighlightWord");
    ASSERT_NE(preset, nullptr);
    ASSERT_NE(headword, nullptr);
    // The shipped default is Nazeka with the Nazeka colors, so nothing has moved yet.
    ASSERT_EQ(preset->currentIndex(), static_cast<int>(ThemePreset::Nazeka));

    headword->setColor(QColor{QStringLiteral("#ff00ff")});
    // The page judges the preset on the state a batch of widget writes leaves, so the check is
    // queued and needs the loop turned once.
    QCoreApplication::processEvents();
    EXPECT_EQ(preset->currentIndex(), static_cast<int>(ThemePreset::Custom));

    fixture.apply();
    EXPECT_EQ(settings::themePreset(), ThemePreset::Custom);
    EXPECT_EQ(PopSettings::colorHighlightWord(), QColor{QStringLiteral("#ff00ff")});
}

// The Shortcuts page keeps its own before-and-after copies, because KConfigDialogManager
// cannot see a KGlobalAccel binding. Apply is what moves one onto the other; before it, the
// page has to report the change so the button lights up.
TEST(SettingsDialogTest, reportsAndAppliesAShortcutChange)
{
    resetSettings();
    const Fixture fixture;
    auto *button = fixture.widget<ShortcutButton>("shortcut_toggle-scanning");
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->keySequence(), HotkeyRegistry::defaultShortcut(QStringLiteral("toggle-scanning")).constFirst());
    EXPECT_FALSE(fixture.applyEnabled());

    // The signal a finished recording emits; KKeySequenceRecorder needs a grabbed keyboard,
    // which an offscreen platform has none of.
    const QKeySequence recorded{Qt::META | Qt::ALT | Qt::Key_K};
    Q_EMIT button->keySequenceChanged(recorded);
    EXPECT_TRUE(fixture.applyEnabled());

    fixture.apply();
    // No registry behind this dialog, so the write goes nowhere; the page still has to end up
    // showing what was applied rather than what it started with.
    EXPECT_EQ(button->keySequence(), recorded);
    EXPECT_FALSE(fixture.applyEnabled());
}

TEST(SettingsDialogTest, leavesEverySettingAloneWhenNothingIsEdited)
{
    resetSettings();
    PopSettings::setMaxResults(7);
    PopSettings::setColorHighlightWord(QColor{QStringLiteral("#123456")});
    PopSettings::setThemePreset(PopSettings::EnumThemePreset::Custom);
    PopSettings::self()->save();

    const Fixture fixture;
    fixture.apply();

    EXPECT_EQ(PopSettings::maxResults(), 7);
    EXPECT_EQ(PopSettings::colorHighlightWord(), QColor{QStringLiteral("#123456")});
    EXPECT_EQ(settings::themePreset(), ThemePreset::Custom);
    EXPECT_TRUE(PopSettings::showTrayIcon());
}

int main(int argc, char **argv)
{
    QApplication app{argc, argv};
    // KColorButton reads the clipboard in its constructor to decide whether its context menu
    // may offer Paste, and the offscreen platform answers mimeData() with a null pointer until
    // something has been put on it. Seeding it is what keeps the Popup Appearance page
    // constructible here; a real session always has a clipboard behind it.
    QGuiApplication::clipboard()->setText(QString());
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
