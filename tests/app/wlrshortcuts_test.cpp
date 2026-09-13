// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The compositor-owned shortcut registry.
//
// hyprland_global_shortcuts_v1 registers an anonymous action: the compositor owns the key
// sequence and the protocol has no request that sets one, so the settings page shows the `bind`
// lines to paste. The cases below cover the two pure
// functions that build those lines and the contract the settings page branches on. The
// registration itself needs a compositor advertising the global and is covered by the live
// registration in tests/nested/CMakeLists.txt.
#include "app/hotkeyregistry.h"
#include "app/wlrshortcuts.h"
#include "capture/hyprlandconfig.h"

#include <QGuiApplication>
#include <QKeySequence>

#include <gtest/gtest.h>

using namespace maru;

TEST(WlrShortcutsTest, buildsTheDispatcherSelector)
{
    EXPECT_EQ(WlrShortcuts::shortcutSelector(QStringLiteral("toggle-scanning")),
              ShortcutRegistry::componentName() + QStringLiteral(":toggle-scanning"));
}

TEST(WlrShortcutsTest, spellsTheModifiersTheWayHyprlandParsesThem)
{
    // Modifiers first and the key last, which is what lets the two configuration languages join
    // them their own way.
    EXPECT_EQ(WlrShortcuts::hyprlandKeyTokens({QKeySequence{Qt::META | Qt::ALT | Qt::Key_J}}),
              QStringList({QStringLiteral("SUPER"), QStringLiteral("ALT"), QStringLiteral("J")}));
    EXPECT_EQ(WlrShortcuts::hyprlandKeyTokens({QKeySequence{Qt::CTRL | Qt::SHIFT | Qt::Key_F1}}),
              QStringList({QStringLiteral("CTRL"), QStringLiteral("SHIFT"), QStringLiteral("F1")}));
    EXPECT_EQ(WlrShortcuts::hyprlandKeyTokens({QKeySequence{Qt::META | Qt::Key_Space}}),
              QStringList({QStringLiteral("SUPER"), QStringLiteral("Space")}));
}

TEST(WlrShortcutsTest, answersAnEmptyKeySpecForAnEmptyList)
{
    EXPECT_TRUE(WlrShortcuts::hyprlandKeyTokens({}).isEmpty());
    EXPECT_TRUE(WlrShortcuts::hyprlandKeyTokens({QKeySequence{}}).isEmpty());
}

// Both languages, spelled out, because the hint is a line the user pastes: a form the compositor
// does not parse is worse than no hint at all.
TEST(WlrShortcutsTest, writesTheBindingInEitherConfigurationLanguage)
{
    const QStringList tokens{QStringLiteral("SUPER"), QStringLiteral("ALT"), QStringLiteral("J")};
    const QString selector = QStringLiteral("io.github.marunine.marupop:toggle-scanning");
    EXPECT_EQ(
        capture::globalShortcutBind(capture::HyprlandConfigLanguage::Lua, tokens, selector),
        QLatin1String(R"(hl.bind("SUPER + ALT + J", hl.dsp.global("io.github.marunine.marupop:toggle-scanning")))"));
    EXPECT_EQ(capture::globalShortcutBind(capture::HyprlandConfigLanguage::Hyprlang, tokens, selector),
              QLatin1String("bind = SUPER ALT, J, global, io.github.marunine.marupop:toggle-scanning"));
    EXPECT_TRUE(capture::globalShortcutBind(capture::HyprlandConfigLanguage::Lua, {}, selector).isEmpty());
}

TEST(WlrShortcutsTest, buildsOneBindLinePerAction)
{
    const WlrShortcuts shortcuts;
    const QStringList ids = ShortcutRegistry::actionIds();
    ASSERT_EQ(ids.size(), 3);
    const capture::HyprlandConfigLanguage language = capture::hyprlandConfig().language;
    for (const QString &id : ids) {
        const QString line = shortcuts.bindingHint(id);
        // The shape the language in hand calls for, asserted rather than re-derived: comparing
        // against globalShortcutBind() alone would only re-execute bindingHint()'s own body and
        // could not fail on a malformed line.
        if (language == capture::HyprlandConfigLanguage::Lua) {
            EXPECT_TRUE(line.startsWith(QLatin1String("hl.bind(\""))) << line.toStdString();
            EXPECT_TRUE(line.contains(QLatin1String("\", hl.dsp.global(\""))) << line.toStdString();
            EXPECT_TRUE(line.endsWith(WlrShortcuts::shortcutSelector(id) + QLatin1String("\"))")))
                << line.toStdString();
        } else {
            EXPECT_TRUE(line.startsWith(QLatin1String("bind = "))) << line.toStdString();
            EXPECT_TRUE(line.contains(QLatin1String(", global, "))) << line.toStdString();
            EXPECT_TRUE(line.endsWith(WlrShortcuts::shortcutSelector(id))) << line.toStdString();
        }
        // The key the line names is the one the KDE path binds by default, so a user reading the
        // two pages sees the same sequence.
        for (const QString &token : WlrShortcuts::hyprlandKeyTokens(ShortcutRegistry::defaultShortcut(id))) {
            EXPECT_TRUE(line.contains(token)) << token.toStdString() << " is absent from " << line.toStdString();
        }
    }
    EXPECT_FALSE(shortcuts.bindingHintHeader().isEmpty());
}

TEST(WlrShortcutsTest, reportsThatTheCompositorOwnsTheKeySequence)
{
    WlrShortcuts shortcuts;
    EXPECT_FALSE(shortcuts.editableShortcuts());
    EXPECT_TRUE(shortcuts.shortcut(QStringLiteral("toggle-scanning")).isEmpty());
    EXPECT_FALSE(shortcuts.setShortcut(QStringLiteral("toggle-scanning"), {QKeySequence{Qt::Key_F5}}));
}

TEST(WlrShortcutsTest, registersNothingWithoutTheGlobal)
{
    // The suite runs under QT_QPA_PLATFORM=offscreen, where there is no Wayland registry at all,
    // so the manager stays unbound and registerActions() is a diagnostic rather than a crash.
    WlrShortcuts shortcuts;
    ASSERT_FALSE(shortcuts.isAvailable());
    EXPECT_FALSE(shortcuts.unavailableReason().isEmpty());
    shortcuts.registerActions();
    for (const QString &id : ShortcutRegistry::actionIds()) {
        EXPECT_FALSE(shortcuts.isRegistered(id));
    }
}

TEST(WlrShortcutsTest, theKGlobalAccelRegistryOwnsItsKeySequences)
{
    // The contract the settings page branches on: one implementation edits, the other shows.
    const HotkeyRegistry hotkeys;
    EXPECT_TRUE(hotkeys.editableShortcuts());
    EXPECT_TRUE(hotkeys.bindingHint(QStringLiteral("toggle-scanning")).isEmpty());
    EXPECT_TRUE(hotkeys.bindingHintHeader().isEmpty());
}

int main(int argc, char **argv)
{
    QGuiApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
