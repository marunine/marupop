// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The three global shortcuts. What a press does after kglobalacceld has it is not testable
// offscreen -- there is no daemon on a test bus, and a registration that reaches none still
// reports success -- so what is covered here is the shape of the component name, the table of
// actions and defaults, and the registry's own bookkeeping, which is what the settings page
// reads and writes.
#include "app/hotkeyregistry.h"

#include <QGuiApplication>
#include <QKeySequence>
#include <QStringList>

#include <gtest/gtest.h>

using namespace maru;

// The component name decides whether a key press ever reaches the process at all, and it does
// so invisibly: kglobalacceld hands any component whose name ends in ".desktop" to a
// KServiceActionComponent, which answers a press by launching a [Desktop Action] group instead
// of signalling whoever registered the action. Nothing in the app can notice -- setShortcut()
// still reports success, and the daemon writes no group to kglobalshortcutsrc because it
// reverts every shortcut that equals its default, which ours do. So pin the shape here; there
// is no other place it can fail loudly.
TEST(HotkeyRegistryTest, componentNameIsTheDesktopBaseNameNotTheFileName)
{
    QGuiApplication::setDesktopFileName(QStringLiteral(MARUPOP_APPLICATION_ID));
    EXPECT_EQ(HotkeyRegistry::componentName(), QStringLiteral(MARUPOP_APPLICATION_ID));
    EXPECT_FALSE(HotkeyRegistry::componentName().endsWith(QLatin1StringView(".desktop")));
}

// Both spellings of the entry reach componentName() as the base name. Qt is no help here:
// QGuiApplication::setDesktopFileName() chops a trailing ".desktop" only when
// QStandardPaths::locate(ApplicationsLocation) resolves the entry, so the same call stores the
// suffix on a machine where MaruPop has yet to be installed, this test runner included. The
// reverse-DNS id contains dots, and only a trailing ".desktop" changes how kglobalacceld
// treats the component, so the check above has to be a suffix check and not a search for a dot.
TEST(HotkeyRegistryTest, componentNameCarriesTheReverseDnsApplicationId)
{
    QGuiApplication::setDesktopFileName(QStringLiteral(MARUPOP_APPLICATION_ID) + QStringLiteral(".desktop"));
    EXPECT_EQ(HotkeyRegistry::componentName(), QStringLiteral(MARUPOP_APPLICATION_ID));
    EXPECT_FALSE(HotkeyRegistry::componentName().endsWith(QLatin1StringView(".desktop")));

    QGuiApplication::setDesktopFileName(QStringLiteral(MARUPOP_APPLICATION_ID));
    EXPECT_TRUE(HotkeyRegistry::componentName().contains(QLatin1Char('.')));
    EXPECT_FALSE(HotkeyRegistry::componentName().endsWith(QLatin1StringView(".desktop")));
}

TEST(HotkeyRegistryTest, listsTheThreeActionsOfTheDesign)
{
    const QStringList ids = HotkeyRegistry::actionIds();
    ASSERT_EQ(ids.size(), 3);
    EXPECT_EQ(ids.at(0), QStringLiteral("toggle-scanning"));
    EXPECT_EQ(ids.at(1), QStringLiteral("copy-word"));
    EXPECT_EQ(ids.at(2), QStringLiteral("pin-popup"));
    for (const QString &id : ids) {
        EXPECT_FALSE(HotkeyRegistry::actionLabel(id).isEmpty()) << id.toStdString();
    }
}

// The ids and the defaults key kglobalshortcutsrc, so a change to either silently drops a
// binding the user set. Keep these sequences consistent with the documented Meta+Alt defaults.
TEST(HotkeyRegistryTest, bindsTheDocumentedDefaultSequences)
{
    EXPECT_EQ(HotkeyRegistry::defaultShortcut(QStringLiteral("toggle-scanning")),
              QList<QKeySequence>{QKeySequence(Qt::META | Qt::ALT | Qt::Key_J)});
    EXPECT_EQ(HotkeyRegistry::defaultShortcut(QStringLiteral("copy-word")),
              QList<QKeySequence>{QKeySequence(Qt::META | Qt::ALT | Qt::Key_C)});
    EXPECT_EQ(HotkeyRegistry::defaultShortcut(QStringLiteral("pin-popup")),
              QList<QKeySequence>{QKeySequence(Qt::META | Qt::ALT | Qt::Key_P)});
    EXPECT_TRUE(HotkeyRegistry::defaultShortcut(QStringLiteral("no-such-action")).isEmpty());
}

TEST(HotkeyRegistryTest, registersAnActionForEveryIdAndReportsItsKeys)
{
    HotkeyRegistry registry;
    const QStringList ids = HotkeyRegistry::actionIds();
    for (const QString &id : ids) {
        EXPECT_FALSE(registry.isRegistered(id)) << id.toStdString();
    }

    registry.registerActions();
    for (const QString &id : ids) {
        EXPECT_TRUE(registry.isRegistered(id)) << id.toStdString();
        // Whatever a daemon answered, or the default where none did. Both are non-empty, and
        // an empty answer here is a registration that never happened.
        EXPECT_FALSE(registry.shortcut(id).isEmpty()) << id.toStdString();
    }
    EXPECT_FALSE(registry.isRegistered(QStringLiteral("no-such-action")));
    EXPECT_TRUE(registry.shortcut(QStringLiteral("no-such-action")).isEmpty());
}

// The settings page writes through setShortcut() and draws itself from shortcut(). Without a
// daemon the write is refused, and the page still has to show what the user recorded, which is
// what the registry's own bookkeeping is for.
TEST(HotkeyRegistryTest, keepsTheKeysAWriteAskedForWhateverTheDaemonAnswers)
{
    HotkeyRegistry registry;
    registry.registerActions();

    const QList<QKeySequence> keys{QKeySequence(Qt::META | Qt::ALT | Qt::Key_K)};
    registry.setShortcut(QStringLiteral("toggle-scanning"), keys);
    EXPECT_EQ(registry.shortcut(QStringLiteral("toggle-scanning")), keys);
    // The other two are untouched by a write to the first.
    EXPECT_EQ(registry.shortcut(QStringLiteral("copy-word")),
              HotkeyRegistry::defaultShortcut(QStringLiteral("copy-word")));

    EXPECT_FALSE(registry.setShortcut(QStringLiteral("no-such-action"), keys));

    // kglobalacceld, where one is listening, stores what this wrote in the tester's own
    // kglobalshortcutsrc: the test HOME does not reach the daemon. Put the default back rather
    // than leaving an invented binding on their session.
    registry.setShortcut(QStringLiteral("toggle-scanning"),
                         HotkeyRegistry::defaultShortcut(QStringLiteral("toggle-scanning")));
}

int main(int argc, char **argv)
{
    // QGuiApplication rather than QCoreApplication: a QAction cannot be constructed without
    // one, and every registration here builds three.
    QGuiApplication app{argc, argv};
    QCoreApplication::setApplicationName(QStringLiteral("marupop"));
    // The registrations below reach the tester's kglobalacceld, which keys them on
    // HotkeyRegistry::componentName() and writes the group into the tester's own
    // kglobalshortcutsrc. Without a desktop file name that name falls back to the application
    // name, so the run would leave a second "marupop" component beside the application's own
    // "io.github.marunine.marupop" one, holding the same three actions.
    QGuiApplication::setDesktopFileName(QStringLiteral(MARUPOP_APPLICATION_ID));
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
