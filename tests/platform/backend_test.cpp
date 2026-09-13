// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The backend factory: every session builds all four services, and each session's capture report
// names what that session requires.
//
// The suite runs under QT_QPA_PLATFORM=offscreen, where there is no Wayland registry, so the
// wlroots implementations are constructed and report themselves unavailable rather than binding
// anything. That is the state a Hyprland backend on a broken session has, and the assertions
// below are that it is a diagnostic rather than a null pointer.
#include "app/shortcutregistry.h"
#include "capture/hyprlandconfig.h"
#include "cursor/cursortracker.h"
#include "platform/backend.h"
#include "platform/session.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QGuiApplication>
#include <QMap>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace maru;
using namespace maru::platform;

namespace
{

const Session kSessions[] = {Session::Unknown, Session::KdePlasma, Session::Hyprland, Session::Wlroots};

} // namespace

TEST(BackendTest, everySessionBuildsAllFourServices)
{
    for (const Session session : kSessions) {
        Backend backend{session};
        EXPECT_EQ(backend.session(), session);
        EXPECT_NE(backend.tracker(), nullptr) << sessionId(session).toStdString();
        EXPECT_NE(backend.frames(), nullptr) << sessionId(session).toStdString();
        EXPECT_NE(backend.lockWatcher(), nullptr) << sessionId(session).toStdString();
        EXPECT_NE(backend.shortcuts(), nullptr) << sessionId(session).toStdString();
    }
}

TEST(BackendTest, onlyTheWlrootsFamilyCapturesItsOwnWindows)
{
    EXPECT_FALSE(Backend{Session::KdePlasma}.capturesOwnWindows());
    EXPECT_FALSE(Backend{Session::Unknown}.capturesOwnWindows());
    EXPECT_TRUE(Backend{Session::Hyprland}.capturesOwnWindows());
    EXPECT_TRUE(Backend{Session::Wlroots}.capturesOwnWindows());
}

TEST(BackendTest, startingAndStoppingTrackingIsSafeWithNoCompositor)
{
    // KdePlasma is left out: KWinScriptRelay::start() writes a KWin script package under HOME
    // and edits kwinrc, which tests/cursor/kwinscriptrelay_test.cpp covers against a fake
    // org.kde.KWin on a private bus. The other three start no such side effect.
    for (const Session session : {Session::Unknown, Session::Hyprland, Session::Wlroots}) {
        Backend backend{session};
        backend.startTracking();
        backend.stopTracking();
    }
}

TEST(BackendTest, anUnknownSessionReportsWhyNothingWorks)
{
    Backend backend{Session::Unknown};
    QSignalSpy availability{backend.tracker(), &cursor::CursorTracker::availabilityChanged};
    ASSERT_TRUE(availability.wait(2000));
    EXPECT_FALSE(availability.constFirst().at(0).toBool());
    EXPECT_FALSE(availability.constFirst().at(1).toString().isEmpty());

    const CaptureReport report = backend.captureReport();
    EXPECT_FALSE(report.authorized);
    EXPECT_FALSE(report.remedy.isEmpty());
    EXPECT_FALSE(report.lines.isEmpty());
}

TEST(BackendTest, aSessionWithNoPointerSourceReportsItOnce)
{
    // Both paths into UnavailableTracker::report() run at startup on such a session: the
    // constructor, which Backend's factory calls, and setTracking(true), which the first
    // ScanController::updateActivity() calls from Application::start(). The reason cannot change
    // between them, and Application::reportFailure() does not deduplicate, so a second emission
    // is a second tray status write and a second identical desktop notification for one fault.
    Backend backend{Session::Unknown};
    QSignalSpy availability{backend.tracker(), &cursor::CursorTracker::availabilityChanged};
    backend.tracker()->setTracking(true);
    ASSERT_TRUE(availability.wait(2000));

    // Both emissions are queued with singleShot(0), so a second one is already in the queue by
    // the time the first is delivered. Draining the queue is what makes it observable.
    const QDeadlineTimer deadline{200};
    while (!deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    EXPECT_EQ(availability.size(), 1) << "one unavailable pointer source was reported to the user twice";
}

TEST(BackendTest, aWlrootsSessionReportsNoPointerSource)
{
    // `cursorpos` is a Hyprland command; no other wlroots compositor answers a global pointer
    // position at all.
    Backend backend{Session::Wlroots};
    QSignalSpy availability{backend.tracker(), &cursor::CursorTracker::availabilityChanged};
    ASSERT_TRUE(availability.wait(2000));
    EXPECT_FALSE(availability.constFirst().at(0).toBool());
}

// The report's two configuration lines are written in whichever of Hyprland's two configuration
// languages the user's own configuration is in, so the case fixes that language rather than
// inheriting it. All four inputs capture::hyprlandConfig() reads are neutralized, not just
// XDG_CONFIG_HOME: a developer with HYPRLAND_CONFIG exported -- which anyone actually running
// Hyprland may well have -- would otherwise get that file's extension, and the first sub-case
// would fail on a machine rather than on a defect.
namespace
{

class HyprlandReportTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(!configHome().isEmpty());
        m_previous = {{"HYPRLAND_CONFIG", qgetenv("HYPRLAND_CONFIG")},
                      {"XDG_CONFIG_HOME", qgetenv("XDG_CONFIG_HOME")},
                      {"XDG_CONFIG_DIRS", qgetenv("XDG_CONFIG_DIRS")},
                      {"HOME", qgetenv("HOME")}};
        qunsetenv("HYPRLAND_CONFIG");
        qputenv("XDG_CONFIG_HOME", configHome().toUtf8());
        qputenv("XDG_CONFIG_DIRS", QByteArray{configHome().toUtf8() + "/dirs"});
        qputenv("HOME", QByteArray{configHome().toUtf8() + "/home"});
    }

    // Restores whatever the caller had, so a later case in this binary reads its own HOME again.
    // TearDown rather than the tail of the case: an ASSERT returns from the case, and a
    // XDG_CONFIG_HOME left pointing at a deleted QTemporaryDir would outlive it.
    void TearDown() override
    {
        for (auto it = m_previous.cbegin(); it != m_previous.cend(); ++it) {
            if (it.value().isEmpty()) {
                qunsetenv(it.key().constData());
            } else {
                qputenv(it.key().constData(), it.value());
            }
        }
    }

    // The directory every one of the four variables points into, so a case can write a
    // hypr/hyprland.conf where the search will find it.
    [[nodiscard]] QString configHome() const
    {
        return m_configHome.path();
    }

private:
    QTemporaryDir m_configHome;
    QMap<QByteArray, QByteArray> m_previous;
};

} // namespace

TEST_F(HyprlandReportTest, namesTheLayerRuleInTheUsersOwnConfigLanguage)
{
    // No hypr/ anywhere, which is what a fresh Hyprland installation looks like before it writes
    // its own configuration: the advice is Lua, because Lua is what it would write.
    const CaptureReport report = captureReportFor(Session::Hyprland, false);
    EXPECT_FALSE(report.authorized);
    const QString joined = report.lines.join(QLatin1Char('\n'));
    EXPECT_TRUE(joined.contains(QLatin1String("hyprland.lua"))) << joined.toStdString();
    EXPECT_TRUE(joined.contains(capture::noScreenShareRule(capture::HyprlandConfigLanguage::Lua)))
        << joined.toStdString();
    EXPECT_TRUE(joined.contains(QLatin1String("hl.permission({"))) << joined.toStdString();
}

TEST_F(HyprlandReportTest, namesHyprlandConfForAUserWhoStillHasOne)
{
    ASSERT_TRUE(QDir{}.mkpath(configHome() + QLatin1String("/hypr")));
    QFile handle{configHome() + QLatin1String("/hypr/hyprland.conf")};
    ASSERT_TRUE(handle.open(QIODevice::WriteOnly));
    handle.close();

    const CaptureReport report = captureReportFor(Session::Hyprland, false);
    const QString joined = report.lines.join(QLatin1Char('\n'));
    EXPECT_TRUE(joined.contains(QLatin1String("hyprland.conf"))) << joined.toStdString();
    EXPECT_TRUE(joined.contains(capture::noScreenShareRule(capture::HyprlandConfigLanguage::Hyprlang)))
        << joined.toStdString();
    EXPECT_TRUE(joined.contains(QLatin1String("screencopy, allow"))) << joined.toStdString();
}

TEST(BackendTest, aBoundScreencopyManagerAuthorizesTheHyprlandSession)
{
    const CaptureReport report = captureReportFor(Session::Hyprland, true);
    EXPECT_TRUE(report.authorized);
    EXPECT_TRUE(report.remedy.isEmpty());
}

TEST(BackendTest, theKdeCaptureReportRunsTheDesktopEntryLookup)
{
    // Whether this binary is authorized depends on the host's installation state, so the verdict
    // is not asserted. What is asserted is that the KDE branch ran the desktop-entry lookup at
    // all, which capture::authorization::describe() reports by naming the executable it resolved
    // /proc/self/exe to. A branch that answered the wlroots lines here would name
    // zwlr_screencopy_manager_v1 instead.
    const CaptureReport report = captureReportFor(Session::KdePlasma, false);
    ASSERT_FALSE(report.lines.isEmpty());
    const QString joined = report.lines.join(QLatin1Char('\n'));
    EXPECT_TRUE(joined.contains(QCoreApplication::applicationFilePath())) << joined.toStdString();
    EXPECT_FALSE(joined.contains(QLatin1String("zwlr_screencopy_manager_v1"))) << joined.toStdString();
}

TEST(BackendTest, aHyprlandSessionWithoutTheShortcutGlobalKeepsTheEditablePage)
{
    // WlrShortcuts is taken where the compositor advertises
    // hyprland_global_shortcuts_manager_v1. The offscreen plugin has no Wayland registry at all,
    // so the backend falls back to the KGlobalAccel registry, which still stores the bindings
    // the settings page edits.
    Backend backend{Session::Hyprland};
    EXPECT_TRUE(backend.shortcuts()->editableShortcuts());
    EXPECT_TRUE(backend.shortcuts()->bindingHintHeader().isEmpty());
}

TEST(BackendTest, everySessionsRegistryCarriesTheSameThreeActions)
{
    const QStringList ids = ShortcutRegistry::actionIds();
    ASSERT_EQ(ids.size(), 3);
    for (const Session session : kSessions) {
        Backend backend{session};
        ShortcutRegistry *shortcuts = backend.shortcuts();
        ASSERT_NE(shortcuts, nullptr) << sessionId(session).toStdString();
        for (const QString &id : ids) {
            EXPECT_FALSE(ShortcutRegistry::actionLabel(id).isEmpty());
            EXPECT_FALSE(ShortcutRegistry::defaultShortcut(id).isEmpty());
            // Nothing is registered before registerActions() runs, whichever implementation the
            // session built.
            EXPECT_FALSE(shortcuts->isRegistered(id)) << sessionId(session).toStdString();
            // The invariant the Shortcuts settings page branches on: a registry either edits its
            // key sequences or offers a line to paste, and never neither. An implementation that
            // answered false to editableShortcuts() and an empty bindingHint() would leave the
            // page with three dead editors and no way to bind anything.
            EXPECT_EQ(shortcuts->editableShortcuts(), shortcuts->bindingHint(id).isEmpty())
                << sessionId(session).toStdString() << ' ' << id.toStdString();
        }
    }
}

int main(int argc, char **argv)
{
    QGuiApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
