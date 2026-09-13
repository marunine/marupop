// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The tray menu and the scanning state. The menu is the whole UI of the application while no
// dialog is open, so its five entries and the check state of the first are what this covers.
// KStatusNotifierItem itself is not asserted on: it registers a service on the session bus,
// which a container has none of.
#include "app/trayicon.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QSignalSpy>

#include <gtest/gtest.h>

using namespace maru;

namespace
{

// The entries a user sees, with the separators left out.
QStringList entryTexts(QMenu *menu)
{
    QStringList texts;
    const QList<QAction *> actions = menu->actions();
    for (const QAction *action : actions) {
        if (!action->isSeparator()) {
            texts.append(action->text());
        }
    }
    return texts;
}

} // namespace

TEST(TrayIconTest, listsTheSixEntriesInOrder)
{
    TrayIcon tray;
    QMenu *menu = tray.contextMenu();
    ASSERT_NE(menu, nullptr);

    const QStringList texts = entryTexts(menu);
    ASSERT_EQ(texts.size(), 6);
    EXPECT_EQ(texts.at(0), QStringLiteral("Enable Scanning"));
    EXPECT_EQ(texts.at(1), QStringLiteral("Lookup Window"));
    EXPECT_EQ(texts.at(2), QStringLiteral("Manage Dictionaries…"));
    EXPECT_EQ(texts.at(3), QStringLiteral("Configure MaruPop…"));
    EXPECT_EQ(texts.at(4), QStringLiteral("About MaruPop"));
    EXPECT_EQ(texts.at(5), QStringLiteral("Quit"));
}

TEST(TrayIconTest, requestsTheLookupWindowAndChecksItsEntryWithItsVisibility)
{
    TrayIcon tray;
    QMenu *menu = tray.contextMenu();
    ASSERT_NE(menu, nullptr);
    QAction *window = menu->actions().at(1);
    ASSERT_EQ(window->text(), QStringLiteral("Lookup Window"));
    ASSERT_TRUE(window->isCheckable());
    QSignalSpy spy{&tray, &TrayIcon::lookupWindowRequested};

    window->trigger();
    EXPECT_EQ(spy.count(), 1);

    tray.setLookupWindowVisible(true);
    EXPECT_TRUE(window->isChecked());
    tray.setLookupWindowVisible(false);
    EXPECT_FALSE(window->isChecked());
}

TEST(TrayIconTest, separatesTheGroupsOfTheMenu)
{
    TrayIcon tray;
    QMenu *menu = tray.contextMenu();
    ASSERT_NE(menu, nullptr);

    // One separator after the scanning toggle and one before Quit, so the destructive entry is
    // never adjacent to the one a mis-click would reach.
    const QList<QAction *> actions = menu->actions();
    int separators = 0;
    for (const QAction *action : actions) {
        if (action->isSeparator()) {
            ++separators;
        }
    }
    EXPECT_EQ(separators, 2);
}

TEST(TrayIconTest, movesTheCheckStateWithTheScanningState)
{
    TrayIcon tray;
    QMenu *menu = tray.contextMenu();
    ASSERT_NE(menu, nullptr);
    QAction *scanning = menu->actions().constFirst();
    ASSERT_TRUE(scanning->isCheckable());
    EXPECT_FALSE(scanning->isChecked());

    tray.setScanning(true);
    EXPECT_TRUE(scanning->isChecked());
    tray.setScanning(false);
    EXPECT_FALSE(scanning->isChecked());
}

TEST(TrayIconTest, requestsAToggleFromTheMenuEntry)
{
    TrayIcon tray;
    QMenu *menu = tray.contextMenu();
    ASSERT_NE(menu, nullptr);
    QSignalSpy spy{&tray, &TrayIcon::toggleScanningRequested};

    menu->actions().constFirst()->trigger();
    EXPECT_EQ(spy.count(), 1);
}

TEST(TrayIconTest, requestsTheDictionaryManagerAndTheSettingsWindow)
{
    TrayIcon tray;
    QMenu *menu = tray.contextMenu();
    ASSERT_NE(menu, nullptr);
    QSignalSpy dictionaries{&tray, &TrayIcon::dictionariesRequested};
    QSignalSpy settings{&tray, &TrayIcon::settingsRequested};
    QSignalSpy about{&tray, &TrayIcon::aboutRequested};
    QSignalSpy quit{&tray, &TrayIcon::quitRequested};
    QSignalSpy lookupWindow{&tray, &TrayIcon::lookupWindowRequested};

    const QList<QAction *> actions = menu->actions();
    for (QAction *action : actions) {
        if (!action->isSeparator() && action->text() != QStringLiteral("Enable Scanning")) {
            action->trigger();
        }
    }
    EXPECT_EQ(lookupWindow.count(), 1);
    EXPECT_EQ(dictionaries.count(), 1);
    EXPECT_EQ(settings.count(), 1);
    EXPECT_EQ(about.count(), 1);
    EXPECT_EQ(quit.count(), 1);
}

int main(int argc, char **argv)
{
    QApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
