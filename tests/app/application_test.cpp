// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The shell, built offscreen: no compositor answers org.kde.KWin, no model file is on disk and
// no dictionary has been imported, which is the state of a machine MaruPop has just been
// installed on. Every one of those has to degrade rather than abort, because the failure a
// user reports is "the tray icon is not there" and that is what this covers.
//
// A live compositor is not asserted on either way: the suite runs in a session as readily as
// in a container, and the two differ only in what the relay and the capture gate report.
#include "app/application.h"
#include "app/lookupwindow.h"
#include "app/trayicon.h"
#include "core/settings.h"

#include <QApplication>
#include <QMenu>
#include <QTimer>

#include <gtest/gtest.h>

using namespace maru;

namespace
{

// The settings a resident start reads, pinned so the suite neither shows the first-run dialog
// nor sends a notification to the tester's desktop over the capture gate this binary fails.
class ResidentSettings
{
public:
    ResidentSettings()
    {
        PopSettings::self()->setDefaults();
        PopSettings::setFirstRunCompleted(true);
        PopSettings::setNotifyOnError(false);
        PopSettings::setScanningEnabled(false);
        PopSettings::self()->save();
    }

    ~ResidentSettings()
    {
        PopSettings::self()->setDefaults();
        PopSettings::self()->save();
    }

    ResidentSettings(const ResidentSettings &) = delete;
    ResidentSettings &operator=(const ResidentSettings &) = delete;
};

// Lets whatever the start queued -- the recognition worker loading a backend, the first
// dictionary update check -- run before the assertions, so a crash on that path is this
// suite's failure rather than the next one's.
void settle(int milliseconds = 200)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace

TEST(ApplicationTest, startsWithNoModelsNoDictionariesAndNoCompositor)
{
    const ResidentSettings settings;
    Application application;
    EXPECT_EQ(application.trayIcon(), nullptr) << "the constructor must not touch the session bus";

    application.start();
    settle();

    ASSERT_NE(application.trayIcon(), nullptr);
    // The menu is the whole user interface while no window is open, so an application that
    // started without it started useless.
    ASSERT_NE(application.trayIcon()->contextMenu(), nullptr);
    EXPECT_EQ(application.trayIcon()->contextMenu()->actions().size(), 8);
}

TEST(ApplicationTest, opensAndClosesTheLookupWindowAndChecksTheTrayEntry)
{
    const ResidentSettings settings;
    Application application;
    application.start();
    settle();
    EXPECT_EQ(application.lookupWindow(), nullptr) << "the window is built on the first request alone";

    EXPECT_TRUE(application.handleCommandLine({QStringLiteral("marupop"), QStringLiteral("--lookup-window")}));
    ASSERT_NE(application.lookupWindow(), nullptr);
    EXPECT_TRUE(application.lookupWindow()->isVisible());
    QAction *entry = application.trayIcon()->contextMenu()->actions().at(1);
    EXPECT_TRUE(entry->isChecked());

    // The tray entry is a toggle: a second request closes the window and clears the check.
    application.toggleLookupWindow();
    EXPECT_FALSE(application.lookupWindow()->isVisible());
    EXPECT_FALSE(entry->isChecked());

    // Applying the settings with the window closed and the popup suppression on reaches the
    // window, the scan controller and the popup without a lookup in hand.
    PopSettings::setHidePopupWhileLookupWindowOpen(true);
    PopSettings::setLookupWindowMaxResults(7);
    application.toggleLookupWindow();
    application.applySettings();
    settle();
    EXPECT_TRUE(application.lookupWindow()->isVisible());
    application.lookupWindow()->close();
}

TEST(ApplicationTest, flipsTheScanningSettingFromTheCommandLine)
{
    const ResidentSettings settings;
    Application application;
    application.start();
    settle();
    ASSERT_FALSE(PopSettings::scanningEnabled());

    EXPECT_TRUE(application.handleCommandLine({QStringLiteral("marupop"), QStringLiteral("--toggle-scanning")}));
    settle();
    EXPECT_TRUE(PopSettings::scanningEnabled());

    EXPECT_TRUE(application.handleCommandLine({QStringLiteral("marupop"), QStringLiteral("--toggle-scanning")}));
    settle();
    EXPECT_FALSE(PopSettings::scanningEnabled());
}

TEST(ApplicationTest, reportsNothingRequestedForACommandLineWithNoVerb)
{
    const ResidentSettings settings;
    Application application;
    application.start();
    settle();

    EXPECT_FALSE(application.handleCommandLine({QStringLiteral("marupop")}));
    // An option the parser does not know -- one KAboutData registered, on a forwarded command
    // line -- is not a verb either, and must not exit the resident process.
    EXPECT_FALSE(application.handleCommandLine({QStringLiteral("marupop"), QStringLiteral("--author")}));
}

TEST(ApplicationTest, appliesSettingsWhileResident)
{
    const ResidentSettings settings;
    Application application;
    application.start();
    settle();

    // The path KConfigDialog::settingsChanged takes. Every service it touches is running with
    // nothing behind it here, which is exactly the case that must not abort.
    PopSettings::setShowTrayIcon(false);
    PopSettings::setMaxResults(3);
    PopSettings::setOcrEngine(PopSettings::EnumOcrEngine::MeikiOcr);
    application.applySettings();
    settle();

    PopSettings::setShowTrayIcon(true);
    application.applySettings();
    settle();
    SUCCEED();
}

int main(int argc, char **argv)
{
    QApplication app{argc, argv};
    QCoreApplication::setApplicationName(QStringLiteral("marupop"));
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
