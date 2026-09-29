// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// A running MaruPop holds the three default combinations. The registration cases use
// Ctrl+Alt+Shift+F23 and Ctrl+Alt+Shift+F24, which are outside the default Windows bindings.
#include "app/shortcutregistry.h"
#include "app/winshortcuts.h"
#include "core/settings.h"
#include "eventloop.h"
#include "win32/messagewindow.h"

#include <QGuiApplication>
#include <QSignalSpy>

#include <KConfigGroup>
#include <KSharedConfig>

#include <gtest/gtest.h>
#include <windows.h>

using namespace maru;

namespace
{

const QKeySequence kUnusedCombination{Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F23};
const QKeySequence kSecondUnusedCombination{Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F24};

void clearStoredShortcuts()
{
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("marupoprc"));
    config->deleteGroup(QStringLiteral("Shortcuts"));
    config->sync();
}

} // namespace

TEST(WinHotKey, mapsTheDefaultsToWinAltAndTheirLetters)
{
    const std::optional<WinHotKey> toggle = winHotKeyFor(QKeySequence{Qt::META | Qt::ALT | Qt::Key_J});
    ASSERT_TRUE(toggle.has_value());
    EXPECT_EQ(toggle->modifiers, static_cast<unsigned>(MOD_WIN | MOD_ALT));
    EXPECT_EQ(toggle->virtualKey, static_cast<unsigned>('J'));

    for (const QString &id : ShortcutRegistry::actionIds()) {
        EXPECT_TRUE(winHotKeyFor(ShortcutRegistry::defaultShortcut(id).constFirst()).has_value()) << id.toStdString();
    }
}

TEST(WinHotKey, mapsFunctionNavigationAndDigitKeys)
{
    EXPECT_EQ(winHotKeyFor(QKeySequence{Qt::CTRL | Qt::SHIFT | Qt::Key_F5}),
              (WinHotKey{static_cast<unsigned>(MOD_CONTROL | MOD_SHIFT), static_cast<unsigned>(VK_F5)}));
    EXPECT_EQ(winHotKeyFor(QKeySequence{Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F24}),
              (WinHotKey{static_cast<unsigned>(MOD_CONTROL | MOD_ALT | MOD_SHIFT), static_cast<unsigned>(VK_F24)}));
    EXPECT_EQ(winHotKeyFor(QKeySequence{Qt::ALT | Qt::Key_Left}),
              (WinHotKey{static_cast<unsigned>(MOD_ALT), static_cast<unsigned>(VK_LEFT)}));
    EXPECT_EQ(winHotKeyFor(QKeySequence{Qt::CTRL | Qt::Key_7}),
              (WinHotKey{static_cast<unsigned>(MOD_CONTROL), static_cast<unsigned>('7')}));
    EXPECT_EQ(winHotKeyFor(QKeySequence{Qt::META | Qt::Key_PageDown}),
              (WinHotKey{static_cast<unsigned>(MOD_WIN), static_cast<unsigned>(VK_NEXT)}));
}

TEST(WinHotKey, mapsPunctuationThroughTheKeyboardLayout)
{
    // The virtual key of '/' depends on the active keyboard layout.
    const SHORT scan = VkKeyScanW(L'/');
    ASSERT_NE(scan, -1) << "the active keyboard layout has no key for '/'";
    const std::optional<WinHotKey> slash = winHotKeyFor(QKeySequence{Qt::CTRL | Qt::ALT | Qt::Key_Slash});
    ASSERT_TRUE(slash.has_value());
    EXPECT_EQ(slash->virtualKey, static_cast<unsigned>(LOBYTE(scan)));
}

TEST(WinHotKey, refusesACombinationWithoutAModifier)
{
    EXPECT_FALSE(winHotKeyFor(QKeySequence{Qt::Key_J}).has_value());
    EXPECT_FALSE(winHotKeyFor(QKeySequence{Qt::Key_F5}).has_value());
    EXPECT_FALSE(winHotKeyFor(QKeySequence{}).has_value());
}

TEST(WinMessageWindow, deliversAPostedMessageThroughTheEventLoop)
{
    unsigned received = 0;
    quintptr receivedParameter = 0;
    win32::MessageWindow window{[&](unsigned message, quintptr wParam, qintptr) {
        if (message != WM_APP + 7) {
            return false;
        }
        received = message;
        receivedParameter = wParam;
        return true;
    }};
    ASSERT_NE(window.handle(), nullptr);
    ASSERT_TRUE(PostMessageW(static_cast<HWND>(window.handle()), WM_APP + 7, 42, 0));
    EXPECT_TRUE(test::waitFor([&] {
        return received != 0;
    }));
    EXPECT_EQ(receivedParameter, 42U);
}

TEST(WinShortcuts, isEditableAndOffersNoBindingHint)
{
    WinShortcuts shortcuts;
    EXPECT_TRUE(shortcuts.editableShortcuts());
    EXPECT_TRUE(shortcuts.bindingHint(QStringLiteral("toggle-scanning")).isEmpty());
    EXPECT_TRUE(shortcuts.bindingHintHeader().isEmpty());
}

TEST(WinShortcuts, emitsTheActionOfAHotKeyId)
{
    WinShortcuts shortcuts;
    QSignalSpy triggered{&shortcuts, &ShortcutRegistry::triggered};
    shortcuts.handleHotKey(1);
    shortcuts.handleHotKey(3);
    // Another component can post an id outside the action ids to the message window.
    shortcuts.handleHotKey(0);
    shortcuts.handleHotKey(4);
    ASSERT_EQ(triggered.size(), 2);
    EXPECT_EQ(triggered.at(0).at(0).toString(), QStringLiteral("toggle-scanning"));
    EXPECT_EQ(triggered.at(1).at(0).toString(), QStringLiteral("pin-popup"));
}

TEST(WinShortcuts, registersAndStoresASequence)
{
    clearStoredShortcuts();
    {
        WinShortcuts shortcuts;
        ASSERT_TRUE(shortcuts.setShortcut(QStringLiteral("copy-word"), {kUnusedCombination}));
        EXPECT_TRUE(shortcuts.isRegistered(QStringLiteral("copy-word")));
        EXPECT_EQ(shortcuts.shortcut(QStringLiteral("copy-word")), QList<QKeySequence>{kUnusedCombination});
    }
    const KConfigGroup group =
        KSharedConfig::openConfig(QStringLiteral("marupoprc"))->group(QStringLiteral("Shortcuts"));
    EXPECT_EQ(group.readEntry("copy-word", QString{}), kUnusedCombination.toString(QKeySequence::PortableText));
    clearStoredShortcuts();
}

TEST(WinShortcuts, refusesACombinationAnotherWindowHoldsAndKeepsThePreviousOne)
{
    clearStoredShortcuts();
    WinShortcuts holder;
    ASSERT_TRUE(holder.setShortcut(QStringLiteral("copy-word"), {kUnusedCombination}));

    WinShortcuts contender;
    ASSERT_TRUE(contender.setShortcut(QStringLiteral("pin-popup"), {kSecondUnusedCombination}));
    // RegisterHotKey() fails with ERROR_HOTKEY_ALREADY_REGISTERED for a combination that the
    // message window of holder holds.
    EXPECT_FALSE(contender.setShortcut(QStringLiteral("pin-popup"), {kUnusedCombination}));
    EXPECT_EQ(contender.shortcut(QStringLiteral("pin-popup")), QList<QKeySequence>{kSecondUnusedCombination});
    EXPECT_TRUE(contender.isRegistered(QStringLiteral("pin-popup")));
    clearStoredShortcuts();
}

TEST(WinShortcuts, removesABindingForAnEmptyList)
{
    clearStoredShortcuts();
    WinShortcuts shortcuts;
    ASSERT_TRUE(shortcuts.setShortcut(QStringLiteral("toggle-scanning"), {kSecondUnusedCombination}));
    ASSERT_TRUE(shortcuts.setShortcut(QStringLiteral("toggle-scanning"), {}));
    EXPECT_FALSE(shortcuts.isRegistered(QStringLiteral("toggle-scanning")));
    EXPECT_TRUE(shortcuts.shortcut(QStringLiteral("toggle-scanning")).isEmpty());
    WinShortcuts other;
    EXPECT_TRUE(other.setShortcut(QStringLiteral("toggle-scanning"), {kSecondUnusedCombination}));
    clearStoredShortcuts();
}

TEST(WinShortcuts, refusesAnUnknownAction)
{
    WinShortcuts shortcuts;
    EXPECT_FALSE(shortcuts.setShortcut(QStringLiteral("no-such-action"), {kUnusedCombination}));
}

int main(int argc, char **argv)
{
    QGuiApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
