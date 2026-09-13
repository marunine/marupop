// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Session detection: the identifier round trip, the MARUPOP_PLATFORM override, and the two
// probes that answer without a Wayland connection.
//
// The third probe reads the Wayland registry and so needs a QGuiApplication on a Wayland
// platform; the suite runs under QT_QPA_PLATFORM=offscreen, where wl::Registry::instance()
// answers nullptr and the probe reports absent. The cases below therefore cover KdePlasma,
// Hyprland and Unknown, and the Wlroots branch is covered by the live registration in
// tests/nested/CMakeLists.txt.
#include "cursor/hyprsocket.h"
#include "platform/session.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace maru::platform;

namespace
{

// Sets an environment variable for the life of the object and restores what was there.
class ScopedEnv
{
public:
    ScopedEnv(const char *name, const QByteArray &value)
        : m_name(name)
        , m_had(qEnvironmentVariableIsSet(name))
        , m_previous(qgetenv(name))
    {
        qputenv(name, value);
    }

    ~ScopedEnv()
    {
        if (m_had) {
            qputenv(m_name, m_previous);
        } else {
            qunsetenv(m_name);
        }
    }

    ScopedEnv(const ScopedEnv &) = delete;
    ScopedEnv &operator=(const ScopedEnv &) = delete;
    ScopedEnv(ScopedEnv &&) = delete;
    ScopedEnv &operator=(ScopedEnv &&) = delete;

private:
    const char *m_name;
    bool m_had;
    QByteArray m_previous;
};

} // namespace

TEST(SessionTest, everyIdentifierRoundTrips)
{
    const Session sessions[] = {Session::Unknown, Session::KdePlasma, Session::Hyprland, Session::Wlroots};
    for (const Session session : sessions) {
        bool recognized = false;
        EXPECT_EQ(parseSessionId(sessionId(session), &recognized), session);
        EXPECT_TRUE(recognized);
    }
}

TEST(SessionTest, parsesTheSpellingsTheOverrideAccepts)
{
    bool recognized = false;
    EXPECT_EQ(parseSessionId(QStringLiteral("plasma"), &recognized), Session::KdePlasma);
    EXPECT_TRUE(recognized);
    EXPECT_EQ(parseSessionId(QStringLiteral("  Hyprland  "), &recognized), Session::Hyprland);
    EXPECT_TRUE(recognized);
    EXPECT_EQ(parseSessionId(QStringLiteral("KDE"), &recognized), Session::KdePlasma);
    EXPECT_TRUE(recognized);
}

TEST(SessionTest, reportsAnUnrecognizedIdentifier)
{
    bool recognized = true;
    EXPECT_EQ(parseSessionId(QStringLiteral("mutter"), &recognized), Session::Unknown);
    EXPECT_FALSE(recognized);
}

TEST(SessionTest, everySessionHasAName)
{
    const Session sessions[] = {Session::Unknown, Session::KdePlasma, Session::Hyprland, Session::Wlroots};
    for (const Session session : sessions) {
        EXPECT_FALSE(sessionName(session).isEmpty());
    }
}

TEST(SessionTest, onlyTheWlrootsFamilyCapturesItsOwnWindows)
{
    // org.kde.KWin.ScreenShot2 renders the scene without the caller's windows through
    // hide-caller-windows; zwlr_screencopy_v1 copies the output's composited frame and offers no
    // such option.
    EXPECT_FALSE(capturesOwnWindows(Session::KdePlasma));
    EXPECT_FALSE(capturesOwnWindows(Session::Unknown));
    EXPECT_TRUE(capturesOwnWindows(Session::Hyprland));
    EXPECT_TRUE(capturesOwnWindows(Session::Wlroots));
}

TEST(SessionTest, theOverrideDecidesTheDetection)
{
    const ScopedEnv platform("MARUPOP_PLATFORM", QByteArrayLiteral("hyprland"));
    EXPECT_EQ(redetect(), Session::Hyprland);
}

TEST(SessionTest, anUnrecognizedOverrideFallsBackToTheProbes)
{
    // What the probes answer on this host with nothing overriding them. Comparing against that
    // rather than against a list of the enum's own values, which no answer could fail.
    Session fromProbes = Session::Unknown;
    {
        const ScopedEnv cleared("MARUPOP_PLATFORM", QByteArray{});
        fromProbes = redetect();
    }
    const ScopedEnv platform("MARUPOP_PLATFORM", QByteArrayLiteral("mutter"));
    EXPECT_EQ(redetect(), fromProbes);
}

TEST(SessionTest, buildsTheSocketPathFromTheTwoVariables)
{
    EXPECT_EQ(maru::cursor::hyprSocketPath(QStringLiteral("/run/user/1000"), QStringLiteral("abc_1")),
              QStringLiteral("/run/user/1000/hypr/abc_1/.socket.sock"));
    EXPECT_TRUE(maru::cursor::hyprSocketPath(QString{}, QStringLiteral("abc_1")).isEmpty());
    EXPECT_TRUE(maru::cursor::hyprSocketPath(QStringLiteral("/run/user/1000"), QString{}).isEmpty());
}

TEST(SessionTest, anExistingHyprlandSocketIsDetectedAsHyprland)
{
    QTemporaryDir runtime;
    ASSERT_TRUE(runtime.isValid());
    const QString signature = QStringLiteral("marupop_test_0");
    ASSERT_TRUE(QDir().mkpath(runtime.path() + QLatin1String("/hypr/") + signature));
    QFile socket(maru::cursor::hyprSocketPath(runtime.path(), signature));
    ASSERT_TRUE(socket.open(QIODevice::WriteOnly));
    socket.close();

    // The Hyprland probe runs first, so this answers Hyprland even on a host where org.kde.KWin
    // owns its name -- which is the whole point of the order. A nested compositor inherits the
    // outer session's DBUS_SESSION_BUS_ADDRESS, and with the probes the other way round a live
    // Hyprland session under Plasma detected as KdePlasma and built all four KDE backends. This case is the unit-level
    // guard for that: an equality rather than a disjunction, because a disjunction admitting
    // KdePlasma would pass with the order restored to the broken one.
    const ScopedEnv platform("MARUPOP_PLATFORM", QByteArray{});
    const ScopedEnv runtimeDir("XDG_RUNTIME_DIR", runtime.path().toLocal8Bit());
    const ScopedEnv instance("HYPRLAND_INSTANCE_SIGNATURE", signature.toLocal8Bit());
    EXPECT_EQ(redetect(), Session::Hyprland);
}
