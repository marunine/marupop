// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The KWin script package compiled into the binary through data/marupop.qrc. The script runs
// inside kwin_wayland, where nothing this project owns can observe it, so the checks here are
// the ones a caller can make before writing the package out: the three D-Bus names it calls
// have to be the ones cursor/ registers, and the KPlugin Id has to equal the directory name
// KWin builds the script path from.
#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <gtest/gtest.h>

namespace
{

QString resourceText(const QString &path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

} // namespace

TEST(KWinScriptTest, callsTheServiceThePopupRegisters)
{
    const QString script = resourceText(QStringLiteral(":/marupop/kwin-script/main.js"));
    ASSERT_FALSE(script.isEmpty());

    // The three names cursor/KWinScriptRelay exports the sink under. A mismatch leaves the
    // script calling into a service nothing answers, which KWin drops without a diagnostic.
    EXPECT_TRUE(script.contains(QStringLiteral("\"io.github.marunine.marupop\"")));
    EXPECT_TRUE(script.contains(QStringLiteral("\"/Cursor\"")));
    EXPECT_TRUE(script.contains(QStringLiteral("\"io.github.marunine.marupop.CursorSink\"")));
    EXPECT_TRUE(script.contains(QStringLiteral("\"Update\"")));
}

TEST(KWinScriptTest, pumpsAtTheTwoConfiguredIntervals)
{
    const QString script = resourceText(QStringLiteral(":/marupop/kwin-script/main.js"));
    ASSERT_FALSE(script.isEmpty());

    // 8 ms while MaruPop tracks, which bounds the send rate at 125 messages per second, and
    // 500 ms otherwise, which is the cost of the relay while MaruPop is not running.
    EXPECT_TRUE(script.contains(QStringLiteral("var ACTIVE_MS = 8;")));
    EXPECT_TRUE(script.contains(QStringLiteral("var IDLE_MS = 500;")));
    // The dirty flag is what decouples the roughly 1000 cursorPosChanged signals per second
    // from the send rate.
    EXPECT_TRUE(script.contains(QStringLiteral("workspace.cursorPosChanged.connect")));
}

TEST(KWinScriptTest, namesThePluginAfterItsDirectory)
{
    const QString text = resourceText(QStringLiteral(":/marupop/kwin-script/metadata.json"));
    ASSERT_FALSE(text.isEmpty());

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    ASSERT_EQ(error.error, QJsonParseError::NoError) << error.errorString().toStdString();
    ASSERT_TRUE(document.isObject());

    const QJsonObject root = document.object();
    EXPECT_EQ(root.value(QStringLiteral("KPackageStructure")).toString(), QStringLiteral("KWin/Script"));
    EXPECT_EQ(root.value(QStringLiteral("X-Plasma-API")).toString(), QStringLiteral("javascript"));

    const QJsonObject plugin = root.value(QStringLiteral("KPlugin")).toObject();
    // KWin builds the script path as kwin/scripts/<pluginId>/contents/code/main.js, so the id
    // has to equal the directory name data/CMakeLists.txt installs the package under and
    // paths::kwinScriptInstallDir() writes it to.
    EXPECT_EQ(plugin.value(QStringLiteral("Id")).toString(), QStringLiteral("marupopcursor"));
    EXPECT_EQ(plugin.value(QStringLiteral("License")).toString(), QStringLiteral("LGPL-3.0-only"));
    // The relay is switched on by cursor/KWinScriptRelay writing marupopcursorEnabled=true
    // into kwinrc, so a package installed system-wide starts disabled and stays disabled for
    // every user who never runs MaruPop.
    EXPECT_FALSE(plugin.value(QStringLiteral("EnabledByDefault")).toBool());
}

TEST(KWinScriptTest, carriesTheDeconjugationRuleSet)
{
    QFile rules{QStringLiteral(":/marupop/deconjugation_rules.json")};
    ASSERT_TRUE(rules.open(QIODevice::ReadOnly));
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(rules.readAll(), &error);
    ASSERT_EQ(error.error, QJsonParseError::NoError) << error.errorString().toStdString();
    ASSERT_TRUE(document.isArray());
    EXPECT_GT(document.array().size(), 0);
}
