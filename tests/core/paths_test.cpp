// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The four directories the application writes to, and the expansion applied to the two
// directory settings a user types. Every path resolves under the per-binary HOME that
// tests/testenvironment.cpp sets, so the suite asserts on a prefix rather than on a literal.
#include "core/paths.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <gtest/gtest.h>

using namespace maru;

TEST(PathsTest, resolvesTheDataDirectoryUnderTheTestHome)
{
    const QString data = paths::dataDir();
    EXPECT_TRUE(data.startsWith(QDir::homePath())) << data.toStdString();
    // AppDataLocation under test mode is $HOME/.qttest/share/<application name>, and the
    // application name is what KAboutData sets; the suite asserts only that the directory was
    // created, which is the contract dataDir() carries.
    EXPECT_TRUE(QFileInfo{data}.isDir()) << data.toStdString();
}

TEST(PathsTest, putsModelsAndDictionariesUnderTheDataDirectory)
{
    const QString data = paths::dataDir();
    EXPECT_EQ(paths::modelsDir(), data + QStringLiteral("/models"));
    EXPECT_EQ(paths::dictionariesDir(), data + QStringLiteral("/dictionaries"));
    EXPECT_TRUE(QFileInfo{paths::modelsDir()}.isDir());
    EXPECT_TRUE(QFileInfo{paths::dictionariesDir()}.isDir());
}

TEST(PathsTest, namesTheKWinScriptDirectoryAfterThePluginId)
{
    // KWin builds the script path as kwin/scripts/<pluginId>/contents/code/main.js, so the
    // last component has to equal the KPlugin Id in data/kwin-script/marupopcursor/metadata.json.
    const QString directory = paths::kwinScriptInstallDir();
    EXPECT_TRUE(directory.endsWith(QStringLiteral("/kwin/scripts/marupopcursor"))) << directory.toStdString();
    EXPECT_TRUE(directory.startsWith(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)))
        << directory.toStdString();
}

TEST(PathsTest, expandsATildeAndAnEnvironmentVariable)
{
    EXPECT_EQ(paths::expandPath(QStringLiteral("~")), QDir::homePath());
    EXPECT_EQ(paths::expandPath(QStringLiteral("~/.config/screen_ai/resources")),
              QDir::homePath() + QStringLiteral("/.config/screen_ai/resources"));
    // A tilde inside the path is a literal character, as it is to a shell.
    EXPECT_EQ(paths::expandPath(QStringLiteral("/opt/~/models")), QStringLiteral("/opt/~/models"));

    qputenv("MARUPOP_TEST_VARIABLE", "/srv/models");
    EXPECT_EQ(paths::expandPath(QStringLiteral("$MARUPOP_TEST_VARIABLE/meiki")), QStringLiteral("/srv/models/meiki"));
    EXPECT_EQ(paths::expandPath(QStringLiteral("${MARUPOP_TEST_VARIABLE}x")), QStringLiteral("/srv/modelsx"));
    qunsetenv("MARUPOP_TEST_VARIABLE");
    // An unset variable expands to nothing, which is the shell's own behavior.
    EXPECT_EQ(paths::expandPath(QStringLiteral("$MARUPOP_TEST_VARIABLE/meiki")), QStringLiteral("/meiki"));
}

TEST(PathsTest, leavesAnEmptyPathEmpty)
{
    EXPECT_EQ(paths::expandPath(QString{}), QString{});
}
