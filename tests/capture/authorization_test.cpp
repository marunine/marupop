// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// capture::authorization against desktop entries this suite installs into its own HOME, which
// is what covers the gate without a Plasma session and without touching the tester's catalog.
//
// The gate decides whether KWin answers org.kde.KWin.ScreenShot2 at all, and a failure appears
// to a user as a capture that produces nothing, so three catalog states are asserted here: a
// path no entry names, a path named by an entry that omits the D-Bus key, and a path named by a
// complete entry.
//
// The three states are three subject paths in one catalog rather than three rebuilds of one
// catalog. KSycoca keeps the database it opened first and compares timestamps at 1 s
// granularity, so a second kbuildsycoca6 run 10 ms after the first leaves this process reading
// the earlier catalog, and a suite written as one rebuild per case passes alone and fails in
// sequence. Building once in SetUpTestSuite() removes the ordering.
//
// kbuildsycoca6 runs as a child process and reads XDG_DATA_HOME and XDG_CACHE_HOME, while this
// process reads the same two directories through QStandardPaths test mode, which is a
// process-local flag a child does not inherit. runKBuildSycoca() therefore passes the test-mode
// paths to the child explicitly; without that the child would index $HOME/.local/share and this
// process would read $HOME/.qttest/share.
#include "capture/authorization.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTextStream>

#include <KSycoca>

#include <gtest/gtest.h>

using namespace maru::capture;

namespace
{

QString dataHome()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
}

QString applicationsDir()
{
    return dataHome() + QStringLiteral("/applications");
}

// The directory holding the three files the entries name. Each one is created with the execute
// bit set, because kbuildsycoca6 drops an entry whose Exec names a file it cannot run.
QString subjectsDir()
{
    return dataHome() + QStringLiteral("/marupop-authorization-subjects");
}

QString subjectPath(const QString &name)
{
    return subjectsDir() + QLatin1Char('/') + name;
}

bool writeFile(const QString &path, const QString &contents)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    QTextStream out(&file);
    out << contents;
    return true;
}

// Writes an entry naming executablePath. An empty list omits the key entirely, which is the
// state of an entry written for an application that never captures.
bool writeEntry(const QString &fileName,
                const QString &executablePath,
                const QStringList &waylandInterfaces,
                const QStringList &dbusInterfaces)
{
    QString contents = QStringLiteral("[Desktop Entry]\nName=MaruPop authorization fixture\nExec=%1\n"
                                      "Type=Application\nNoDisplay=true\n")
                           .arg(executablePath);
    // A comma, which is what KConfig splits a QStringList entry on and therefore what
    // KService::property<QStringList>() in capture::authorization reads back. It is also the
    // separator the shipped entry, tests/harness/nested-session.sh and every upstream entry use --
    // xdg-desktop-portal-kde/data/org.freedesktop.impl.portal.desktop.kde.desktop.in declares three
    // Wayland globals that way. A semicolon put the whole list in element 0, which no case noticed
    // while both required lists held exactly one element.
    if (!waylandInterfaces.isEmpty()) {
        contents += QStringLiteral("X-KDE-Wayland-Interfaces=%1\n").arg(waylandInterfaces.join(QLatin1Char(',')));
    }
    if (!dbusInterfaces.isEmpty()) {
        contents += QStringLiteral("X-KDE-DBUS-Restricted-Interfaces=%1\n").arg(dbusInterfaces.join(QLatin1Char(',')));
    }
    return writeFile(applicationsDir() + QLatin1Char('/') + fileName, contents);
}

// Runs kbuildsycoca6 over the suite's own data directory and waits for it. Returns false where
// the binary is absent or exits non-zero, which is what the cases skip on.
bool runKBuildSycoca()
{
    const QString program = QStandardPaths::findExecutable(QStringLiteral("kbuildsycoca6"));
    if (program.isEmpty()) {
        return false;
    }

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("XDG_DATA_HOME"), dataHome());
    environment.insert(QStringLiteral("XDG_CACHE_HOME"),
                       QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation));
    // XDG_DATA_DIRS is inherited rather than overridden. KSycoca names its database file after
    // a hash of the directory list it indexed, so a child given a different list writes a file
    // this process never opens: overriding it left two ksycoca6_en_* files in the cache
    // directory and every lookup answering "no match". The subjects live under the suite's own
    // HOME, so indexing the system catalog as well matches nothing extra.

    QProcess process;
    process.setProcessEnvironment(environment);
    process.setProgram(program);
    process.setArguments({QStringLiteral("--noincremental")});
    process.start();
    if (!process.waitForFinished(30000)) {
        // ~QProcess would kill the child while it is writing the database, leaving a truncated
        // ksycoca6_* file that every later run of this suite opens and finds no entry in.
        // Killing it here and waiting leaves the cache absent instead, which KSycoca rebuilds.
        process.kill();
        process.waitForFinished(5000);
        return false;
    }
    KSycoca::self()->ensureCacheValid();
    return process.exitCode() == 0;
}

// The names of the three subjects, fixed so a case names the state it asserts on.
constexpr QLatin1StringView kAuthorized("authorized-subject");
constexpr QLatin1StringView kPartial("partial-subject");
constexpr QLatin1StringView kUnlisted("unlisted-subject");
// An entry with two interfaces per key, which is what tells the separator apart: a list joined
// with the wrong character comes back as one element holding both names.
constexpr QLatin1StringView kMultiple("multiple-subject");

const QStringList &extraWaylandInterfaces()
{
    static const QStringList interfaces = {QStringLiteral("zkde_screencast_unstable_v1"),
                                           QStringLiteral("org_kde_kwin_fake_input")};
    return interfaces;
}

const QStringList &extraDBusInterfaces()
{
    static const QStringList interfaces = {QStringLiteral("org.kde.KWin.ScreenShot2"),
                                           QStringLiteral("org.kde.KWin.ScreencastingV1")};
    return interfaces;
}

// Writes the fixture and builds the catalog once, before any test runs.
// KSycoca caches the first database it opens and compares timestamps at one-second
// resolution. A global environment installs the entries before either test suite
// can resolve a service, preventing results from depending on suite order or a warm cache.
class Fixture : public ::testing::Environment
{
public:
    void SetUp() override
    {
        QDir().mkpath(applicationsDir());
        QDir().mkpath(subjectsDir());
        bool written = true;
        for (const QLatin1StringView name : {kAuthorized, kPartial, kUnlisted, kMultiple}) {
            written = written && writeFile(subjectPath(name), QStringLiteral("#!/bin/sh\nexit 0\n"));
            written = written &&
                      QFile::setPermissions(subjectPath(name),
                                            QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup |
                                                QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
        }

        written = written && writeEntry(QStringLiteral("marupop-authorized.desktop"),
                                        subjectPath(kAuthorized),
                                        authorization::requiredWaylandInterfaces(),
                                        authorization::requiredDBusInterfaces());
        // The Wayland key alone, which is the entry an application that screencasts but takes
        // no screenshot would carry. The capture path needs org.kde.KWin.ScreenShot2.
        written = written && writeEntry(QStringLiteral("marupop-partial.desktop"),
                                        subjectPath(kPartial),
                                        authorization::requiredWaylandInterfaces(),
                                        {});
        written = written && writeEntry(QStringLiteral("marupop-multiple.desktop"),
                                        subjectPath(kMultiple),
                                        extraWaylandInterfaces(),
                                        extraDBusInterfaces());
        // kUnlisted deliberately gets no entry.

        // A failed write is reported as its own condition. kbuildsycoca6 exits 0 over an empty
        // catalog, so without this a read-only HOME would surface as capture::authorization
        // matching no entry rather than as the fixture never reaching disk.
        fixtureWritten = written;
        catalogBuilt = written && runKBuildSycoca();
    }

    void TearDown() override
    {
        QDir(applicationsDir()).removeRecursively();
        QDir(subjectsDir()).removeRecursively();
    }

    static bool catalogBuilt;
    static bool fixtureWritten;
};

bool Fixture::catalogBuilt = false;
bool Fixture::fixtureWritten = false;

class AuthorizationTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(Fixture::fixtureWritten)
            << "the desktop entries and their subjects could not be written under " << dataHome().toStdString();
        if (!Fixture::catalogBuilt) {
            GTEST_SKIP() << "kbuildsycoca6 is absent or exited non-zero, so no service catalog was built";
        }
    }
};

} // namespace

TEST(AuthorizationInterfacesTest, requiresOnlyTheScreenshotInterface)
{
    EXPECT_EQ(authorization::requiredDBusInterfaces(), QStringList{QStringLiteral("org.kde.KWin.ScreenShot2")});
    EXPECT_TRUE(authorization::requiredWaylandInterfaces().isEmpty());
}

TEST(AuthorizationInterfacesTest, reportsAPathWithNoFileAsAbsent)
{
    const authorization::Report report =
        authorization::checkExecutable(QStringLiteral("/nonexistent/marupop-does-not-exist"));

    EXPECT_FALSE(report.executableExists);
    EXPECT_FALSE(report.authorized());
    EXPECT_TRUE(report.desktopEntryPath.isEmpty());
    // The path is reported as given rather than canonicalized, because an absent file
    // canonicalizes to an empty string that would match every equally absent Exec.
    EXPECT_EQ(report.executablePath, QStringLiteral("/nonexistent/marupop-does-not-exist"));
    EXPECT_TRUE(
        authorization::describe(report).join(QLatin1Char('\n')).contains(QStringLiteral("Executable not found")));
}

TEST_F(AuthorizationTest, reportsAnExecutableNoEntryNamesAsUnauthorized)
{
    const authorization::Report report = authorization::checkExecutable(subjectPath(kUnlisted));

    EXPECT_TRUE(report.executableExists);
    EXPECT_TRUE(report.desktopEntryPath.isEmpty());
    EXPECT_FALSE(report.authorized());
    // Every required interface is missing for a path with no entry.
    EXPECT_EQ(report.missingInterfaces().size(),
              authorization::requiredWaylandInterfaces().size() + authorization::requiredDBusInterfaces().size());
    const QString described = authorization::describe(report).join(QLatin1Char('\n'));
    EXPECT_TRUE(described.contains(QStringLiteral("kbuildsycoca6"))) << described.toStdString();
}

TEST_F(AuthorizationTest, reportsACompleteEntryAsAuthorized)
{
    const authorization::Report report = authorization::checkExecutable(subjectPath(kAuthorized));

    ASSERT_FALSE(report.desktopEntryPath.isEmpty()) << "no entry matched " << subjectPath(kAuthorized).toStdString();
    EXPECT_TRUE(report.authorized());
    EXPECT_TRUE(report.missingInterfaces().isEmpty());
    EXPECT_TRUE(report.dbusInterfaces.contains(QStringLiteral("org.kde.KWin.ScreenShot2")));
    EXPECT_TRUE(report.waylandInterfaces.isEmpty());
    EXPECT_TRUE(authorization::describe(report).join(QLatin1Char('\n')).contains(QStringLiteral("authorized")));
}

TEST_F(AuthorizationTest, reportsTheOmittedInterfaceOfAnIncompleteEntry)
{
    const authorization::Report report = authorization::checkExecutable(subjectPath(kPartial));

    ASSERT_FALSE(report.desktopEntryPath.isEmpty());
    EXPECT_FALSE(report.authorized());
    EXPECT_EQ(report.missingInterfaces(), QStringList{QStringLiteral("org.kde.KWin.ScreenShot2")});
    EXPECT_TRUE(authorization::describe(report)
                    .join(QLatin1Char('\n'))
                    .contains(QStringLiteral("Authorization missing: org.kde.KWin.ScreenShot2")));
}

// Two interfaces per key must read back as two elements. KConfig splits QStringList
// entries on commas. A one-interface fixture cannot expose an incorrect separator.
TEST_F(AuthorizationTest, readsEveryInterfaceOfAnEntryThatDeclaresSeveral)
{
    const authorization::Report report = authorization::checkExecutable(subjectPath(kMultiple));

    ASSERT_FALSE(report.desktopEntryPath.isEmpty()) << "no entry matched " << subjectPath(kMultiple).toStdString();
    EXPECT_EQ(report.waylandInterfaces.size(), 2);
    EXPECT_EQ(report.dbusInterfaces.size(), 2);
    for (const QString &interface : extraWaylandInterfaces()) {
        EXPECT_TRUE(report.waylandInterfaces.contains(interface)) << interface.toStdString();
    }
    for (const QString &interface : extraDBusInterfaces()) {
        EXPECT_TRUE(report.dbusInterfaces.contains(interface)) << interface.toStdString();
    }
    // Every required interface is present among the several, so the entry authorizes.
    EXPECT_TRUE(report.missingInterfaces().isEmpty());
    EXPECT_TRUE(report.authorized());
}

TEST_F(AuthorizationTest, matchesTheEntryThroughTheCanonicalPathOfASymbolicLink)
{
    // KWin canonicalizes /proc/<pid>/exe before it compares, so a build tree reached through a
    // symbolic link matches the entry naming the real file.
    const QString link = subjectsDir() + QStringLiteral("/authorized-through-a-link");
    QFile::remove(link);
    ASSERT_TRUE(QFile::link(subjectPath(kAuthorized), link));

    const authorization::Report report = authorization::checkExecutable(link);

    EXPECT_EQ(report.executablePath, QFileInfo(subjectPath(kAuthorized)).canonicalFilePath());
    EXPECT_TRUE(report.authorized());
}

TEST(AuthorizationInterfacesTest, checksTheRunningProcessByItsOwnExecutablePath)
{
    const authorization::Report report = authorization::checkThisProcess();
    EXPECT_EQ(report.executablePath, QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath());
    EXPECT_TRUE(report.executableExists);
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    // Registered before RUN_ALL_TESTS(), so the entries exist and the catalog is built before the
    // first case resolves a service and fixes the KSycoca database for the process.
    ::testing::AddGlobalTestEnvironment(new Fixture);
    return RUN_ALL_TESTS();
}
