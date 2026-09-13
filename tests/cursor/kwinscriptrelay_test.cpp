// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The two side effects the relay has outside its own process -- the KWin script package on
// disk and the kwinrc key -- against a temporary directory and a temporary configuration file,
// plus one live bootstrap against the running compositor.
//
// The live case is gated on org.kde.KWin owning its bus name and on MARUPOP_LIVE_KWIN=1,
// because it loads a script into the compositor. It unloads it again, and asserts that
// isScriptLoaded reports false afterwards.
#include "core/paths.h"
#include "cursor/cursorsink.h"
#include "cursor/kwinscriptrelay.h"
#include "eventloop.h"
#include "fakekwin.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <KConfigGroup>
#include <KSharedConfig>

#include <gtest/gtest.h>

using namespace maru::cursor;
using maru::test::waitFor;

namespace
{

QByteArray fileBytes(const QString &path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QByteArray resourceBytes(const QString &path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

bool liveKWinRequested()
{
    return qEnvironmentVariable("MARUPOP_LIVE_KWIN") == QLatin1StringView("1");
}

// isScriptLoaded over the session bus, for the assertion that the live case left nothing loaded.
bool scriptLoaded()
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                          QStringLiteral("/Scripting"),
                                                          QStringLiteral("org.kde.kwin.Scripting"),
                                                          QStringLiteral("isScriptLoaded"));
    message.setArguments({KWinScriptRelay::pluginId()});
    const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 5000);
    return reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty() &&
           reply.arguments().constFirst().toBool();
}

} // namespace

TEST(KWinScriptRelayTest, namesThePluginAfterTheInstallDirectory)
{
    EXPECT_EQ(KWinScriptRelay::pluginId(), QStringLiteral("marupopcursor"));
    // KWin builds the script path as kwin/scripts/<pluginId>/contents/code/main.js, so the last
    // component of the install directory has to be the plugin id.
    EXPECT_EQ(QFileInfo(maru::paths::kwinScriptInstallDir()).fileName(), KWinScriptRelay::pluginId());
    EXPECT_EQ(KWinScriptRelay::scriptPath(QStringLiteral("/tmp/x")), QStringLiteral("/tmp/x/contents/code/main.js"));
}

TEST(KWinScriptRelayTest, writesThePackageFromTheQtResources)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString installDir = directory.filePath(QStringLiteral("marupopcursor"));

    EXPECT_FALSE(KWinScriptRelay::packageMatchesResources(installDir));
    ASSERT_TRUE(KWinScriptRelay::writePackage(installDir));
    EXPECT_TRUE(KWinScriptRelay::packageMatchesResources(installDir));

    // The three files, with the content the resources carry.
    EXPECT_EQ(fileBytes(installDir + QStringLiteral("/metadata.json")),
              resourceBytes(QStringLiteral(":/marupop/kwin-script/metadata.json")));
    EXPECT_EQ(fileBytes(KWinScriptRelay::scriptPath(installDir)),
              resourceBytes(QStringLiteral(":/marupop/kwin-script/main.js")));
    const QByteArray marker = fileBytes(installDir + QStringLiteral("/.marupop-version"));
    EXPECT_FALSE(marker.isEmpty());
    EXPECT_TRUE(marker.endsWith('\n'));

    // The KPlugin Id in the written metadata has to equal the directory name.
    const QJsonObject metadata =
        QJsonDocument::fromJson(fileBytes(installDir + QStringLiteral("/metadata.json"))).object();
    EXPECT_EQ(metadata.value(QStringLiteral("KPlugin")).toObject().value(QStringLiteral("Id")).toString(),
              KWinScriptRelay::pluginId());
}

TEST(KWinScriptRelayTest, rewritesAPackageWhoseContentDiffers)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString installDir = directory.filePath(QStringLiteral("marupopcursor"));
    ASSERT_TRUE(KWinScriptRelay::writePackage(installDir));

    QFile script{KWinScriptRelay::scriptPath(installDir)};
    ASSERT_TRUE(script.open(QIODevice::WriteOnly | QIODevice::Truncate));
    script.write("// edited\n");
    script.close();
    EXPECT_FALSE(KWinScriptRelay::packageMatchesResources(installDir));

    ASSERT_TRUE(KWinScriptRelay::writePackage(installDir));
    EXPECT_TRUE(KWinScriptRelay::packageMatchesResources(installDir));
    EXPECT_EQ(fileBytes(KWinScriptRelay::scriptPath(installDir)),
              resourceBytes(QStringLiteral(":/marupop/kwin-script/main.js")));
}

TEST(KWinScriptRelayTest, leavesAnUpToDatePackageUntouched)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString installDir = directory.filePath(QStringLiteral("marupopcursor"));
    ASSERT_TRUE(KWinScriptRelay::writePackage(installDir));

    const QDateTime written = QFileInfo(KWinScriptRelay::scriptPath(installDir)).lastModified();
    ASSERT_TRUE(KWinScriptRelay::writePackage(installDir));
    EXPECT_EQ(QFileInfo(KWinScriptRelay::scriptPath(installDir)).lastModified(), written);
}

TEST(KWinScriptRelayTest, enablesThePluginInTheConfigurationFileOnce)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString configPath = directory.filePath(QStringLiteral("kwinrc"));
    KSharedConfig::Ptr config = KSharedConfig::openConfig(configPath, KConfig::SimpleConfig);

    EXPECT_FALSE(KWinScriptRelay::isEnabledInConfig(config));
    EXPECT_TRUE(KWinScriptRelay::enableInConfig(config));
    EXPECT_TRUE(KWinScriptRelay::isEnabledInConfig(config));
    // The second call writes nothing, so KWin is not asked to reload a file that did not change.
    EXPECT_FALSE(KWinScriptRelay::enableInConfig(config));

    // The key reaches the file, under the group name KWin's Scripting::queryScriptsToLoad reads.
    const QByteArray written = fileBytes(configPath);
    EXPECT_TRUE(written.contains("[Plugins]"));
    EXPECT_TRUE(written.contains("marupopcursorEnabled=true"));
}

TEST(KWinScriptRelayTest, readsAnExistingDisabledValue)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const QString configPath = directory.filePath(QStringLiteral("kwinrc"));
    KSharedConfig::Ptr config = KSharedConfig::openConfig(configPath, KConfig::SimpleConfig);
    KConfigGroup plugins{config, QStringLiteral("Plugins")};
    plugins.writeEntry(QStringLiteral("marupopcursorEnabled"), false);
    plugins.sync();

    EXPECT_FALSE(KWinScriptRelay::isEnabledInConfig(config));
    EXPECT_TRUE(KWinScriptRelay::enableInConfig(config));
    EXPECT_TRUE(KWinScriptRelay::isEnabledInConfig(config));
}

TEST(KWinScriptRelayTest, reportsTheReasonWhereKWinIsAbsent)
{
    if (KWinScriptRelay::kwinAvailable()) {
        GTEST_SKIP() << "org.kde.KWin owns its bus name; the absent-compositor path cannot be reached";
    }
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    KWinScriptRelay relay{
        directory.filePath(QStringLiteral("marupopcursor")),
        KSharedConfig::openConfig(directory.filePath(QStringLiteral("kwinrc")), KConfig::SimpleConfig)};
    QSignalSpy spy{&relay, &CursorTracker::availabilityChanged};
    relay.start();
    ASSERT_TRUE(waitFor(
        [&spy] {
            return spy.count() >= 1;
        },
        2000));
    EXPECT_FALSE(spy.at(0).at(0).toBool());
    EXPECT_FALSE(spy.at(0).at(1).toString().isEmpty());
    EXPECT_FALSE(relay.isAvailable());
}

TEST(KWinScriptRelayTest, tracksTheFlagThroughToTheSink)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    KWinScriptRelay relay{
        directory.filePath(QStringLiteral("marupopcursor")),
        KSharedConfig::openConfig(directory.filePath(QStringLiteral("kwinrc")), KConfig::SimpleConfig)};
    EXPECT_FALSE(relay.isTracking());
    EXPECT_FALSE(relay.sink()->isTracking());
    relay.setTracking(true);
    EXPECT_TRUE(relay.isTracking());
    EXPECT_TRUE(relay.sink()->isTracking());
}

TEST(KWinScriptRelayTest, bootstrapsAgainstTheRunningCompositor)
{
    // Under the harness both gates are something the harness supplies, so a skip there is a defect
    // in the registration rather than a missing resource -- and a silent one, since
    // kwinscriptrelay_test_nested would still report as passed and the CI guard counts ctest-level
    // skips alone.
    const bool underHarness = qEnvironmentVariable("MARUPOP_NESTED_SESSION") == QLatin1StringView("1");
    if (underHarness) {
        ASSERT_TRUE(KWinScriptRelay::kwinAvailable()) << "the harness started no compositor that owns org.kde.KWin";
        ASSERT_TRUE(liveKWinRequested()) << "MARUPOP_LIVE_KWIN=1 is unset under the harness; the ENVIRONMENT of "
                                            "maru_add_nested_test(kwinscriptrelay_test_nested …) is what sets it";
    }
    if (!KWinScriptRelay::kwinAvailable()) {
        // This suite is registered through maru_add_gtest_dbus(), so its bus is a private one that
        // no compositor owns a name on, and setting MARUPOP_LIVE_KWIN=1 by hand on a live Plasma
        // session reaches this branch rather than the case. kwinscriptrelay_test_nested is the
        // registration that runs it, against the compositor the harness starts.
        GTEST_SKIP() << "org.kde.KWin does not own this bus; run the case through "
                        "ctest -R kwinscriptrelay_test_nested, which starts a compositor on the bus";
    }
    if (!liveKWinRequested()) {
        GTEST_SKIP() << "MARUPOP_LIVE_KWIN=1 is not set; the live case loads a script into the compositor";
    }
    ASSERT_FALSE(scriptLoaded()) << "a marupopcursor script is already loaded in this session";
    // The script calls io.github.marunine.marupop, which KDBusService owns in the running
    // application. Nothing answers the call unless this process owns the name for the duration.
    const QString serviceName = QStringLiteral("io.github.marunine.marupop");
    if (!QDBusConnection::sessionBus().registerService(serviceName)) {
        GTEST_SKIP() << "io.github.marunine.marupop is already owned; a MaruPop instance is running";
    }

    // The install directory and the configuration file both resolve under the test HOME, which
    // tests/CMakeLists.txt and tests/testenvironment.cpp redirect, so the tester's own
    // ~/.local/share/kwin and ~/.config/kwinrc are untouched. KWin never finds a package there,
    // so this exercises the loadScript() fast path rather than the packaged path.
    auto relay = std::make_unique<KWinScriptRelay>();
    QSignalSpy availability{relay.get(), &CursorTracker::availabilityChanged};
    QSignalSpy positions{relay.get(), &CursorTracker::positionChanged};
    // False, so the script stays on its 500 ms heartbeat and reports a position without the
    // pointer having to move.
    relay->setTracking(false);
    relay->start();

    ASSERT_TRUE(waitFor(
        [&relay] {
            return relay->isAvailable();
        },
        15000))
        << (availability.isEmpty() ? std::string{"no availability was reported"}
                                   : availability.constLast().at(1).toString().toStdString());
    EXPECT_TRUE(relay->unavailableReason().isEmpty());
    EXPECT_TRUE(scriptLoaded());

    // The heartbeat carries a position and a screen name.
    ASSERT_TRUE(waitFor(
        [&positions] {
            return positions.count() >= 1;
        },
        3000));
    const CursorSink::LatencyStats latency = relay->sink()->latency();
    EXPECT_GT(latency.samples, 0U);
    qInfo("live relay: %llu samples, latency last %.3f ms, min %.3f ms, mean %.3f ms, max %.3f ms",
          static_cast<unsigned long long>(latency.samples),
          latency.lastMs,
          latency.minMs,
          latency.meanMs,
          latency.maxMs);
    // Under a second, which is the bound a delivery over the session bus in the same session has.
    EXPECT_LT(latency.maxMs, 1000.0);

    // The destructor unloads the script and leaves the package installed.
    relay.reset();
    EXPECT_FALSE(scriptLoaded());
    QDBusConnection::sessionBus().unregisterService(serviceName);
}

namespace
{

// The relay against maru::test::FakeKWin on the private session bus.
// Covers loadScript, run on /Scripting/Script<id>, isScriptLoaded and unloadScript
// without requiring a running compositor.
class KWinScriptRelayFakeTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        kwin = std::make_unique<maru::test::FakeKWin>();
        if (!kwin->isRegistered()) {
            GTEST_SKIP() << kwin->skipReason().toStdString();
        }
        ASSERT_TRUE(directory.isValid());
    }

    void TearDown() override
    {
        relay.reset();
        kwin.reset();
    }

    // A relay writing its package and its kwinrc under a QTemporaryDir, so the tester's
    // ~/.local/share/kwin and ~/.config/kwinrc are untouched.
    KWinScriptRelay *makeRelay()
    {
        relay = std::make_unique<KWinScriptRelay>(
            directory.filePath(QStringLiteral("marupopcursor")),
            KSharedConfig::openConfig(directory.filePath(QStringLiteral("kwinrc")), KConfig::SimpleConfig));
        return relay.get();
    }

    QTemporaryDir directory;
    std::unique_ptr<maru::test::FakeKWin> kwin;
    std::unique_ptr<KWinScriptRelay> relay;
};

} // namespace

TEST_F(KWinScriptRelayFakeTest, reportsTheCompositorAsPresentWhileTheNameIsOwned)
{
    EXPECT_TRUE(KWinScriptRelay::kwinAvailable());
}

TEST_F(KWinScriptRelayFakeTest, acceptsThePackagedScriptTheCompositorLoadedItself)
{
    // The path a compositor that read the KPackage takes: isScriptLoaded answers true on the
    // first poll, so no loadScript call follows.
    kwin->scripting()->markLoaded(KWinScriptRelay::pluginId());

    KWinScriptRelay *relay = makeRelay();
    relay->start();

    ASSERT_TRUE(waitFor(
        [relay] {
            return relay->isAvailable();
        },
        5000))
        << relay->unavailableReason().toStdString();
    EXPECT_TRUE(relay->unavailableReason().isEmpty());
    EXPECT_TRUE(kwin->scripting()->loadedScripts().isEmpty());
}

TEST_F(KWinScriptRelayFakeTest, loadsRunsAndVerifiesTheScriptWhereThePackagedPathDoesNotLoad)
{
    // No markLoaded, so isScriptLoaded answers false for the whole 2000 ms poll window
    // (10 attempts at 200 ms) and the relay falls back to loadScript on the absolute path.
    KWinScriptRelay *relay = makeRelay();
    relay->start();

    ASSERT_TRUE(waitFor(
        [relay] {
            return relay->isAvailable();
        },
        20000))
        << relay->unavailableReason().toStdString();

    // loadScript takes the plugin id as its second argument, which is the name isScriptLoaded
    // is then asked about.
    EXPECT_TRUE(kwin->scripting()->loadedScripts().contains(KWinScriptRelay::pluginId()))
        << kwin->scripting()->loadedScripts().join(QLatin1Char(' ')).toStdString();
    // loadScript() only constructs the script object; run() is what evaluates it, and a
    // bootstrap that omitted it would leave the relay reporting available with no positions.
    EXPECT_EQ(kwin->scripting()->runScripts(), QStringList{QStringLiteral("/Scripting/Script0")});
    EXPECT_TRUE(relay->unavailableReason().isEmpty());
}

TEST_F(KWinScriptRelayFakeTest, asksTheCompositorToRereadItsConfiguration)
{
    KWinScriptRelay *relay = makeRelay();
    relay->start();

    // The kwinrc key alone does not load a plugin: KWin reads it on reconfigure().
    ASSERT_TRUE(waitFor(
        [this] {
            return kwin->core()->reconfigureCount() > 0;
        },
        15000));
}

TEST_F(KWinScriptRelayFakeTest, reportsTheReasonWhereTheScriptNeverLoads)
{
    // A compositor that accepts loadScript and reports the script as absent afterwards, which
    // is the state a package KWin refuses to read produces.
    kwin->scripting()->acceptsLoad = false;

    KWinScriptRelay *relay = makeRelay();
    QSignalSpy availability{relay, &CursorTracker::availabilityChanged};
    relay->start();

    ASSERT_TRUE(waitFor(
        [&availability] {
            return !availability.isEmpty();
        },
        20000));
    EXPECT_FALSE(relay->isAvailable());
    EXPECT_FALSE(relay->unavailableReason().isEmpty());
}

TEST_F(KWinScriptRelayFakeTest, unloadsTheScriptOnStop)
{
    kwin->scripting()->markLoaded(KWinScriptRelay::pluginId());
    KWinScriptRelay *relay = makeRelay();
    relay->start();
    ASSERT_TRUE(waitFor(
        [relay] {
            return relay->isAvailable();
        },
        5000));

    relay->stop();
    ASSERT_TRUE(waitFor(
        [this] {
            return !kwin->scripting()->unloadedScripts().isEmpty();
        },
        5000));
    EXPECT_TRUE(kwin->scripting()->unloadedScripts().contains(KWinScriptRelay::pluginId()));
    // The package stays on disk, so the entry stays listed under System Settings, Window
    // Management, KWin Scripts.
    EXPECT_TRUE(KWinScriptRelay::packageMatchesResources(relay->installDir()));
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
