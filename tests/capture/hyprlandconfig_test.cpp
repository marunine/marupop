// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Hyprland configuration advice must use the compositor's selected language.
// For example, `layerrule = noscreenshare, marupop-popup` lacks the value required by
// Hyprland 0.56.2. Test both the language selection and the generated rules.
//
// Each case sets HOME, XDG_CONFIG_HOME, XDG_CONFIG_DIRS and HYPRLAND_CONFIG so no
// configuration from the developer's session affects the result.
#include "capture/hyprlandconfig.h"

#include <QDir>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace maru::capture;

namespace
{

class HyprlandConfigTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(m_dir.isValid());
        // Cleared rather than left alone: an inherited value of any of the four would make the
        // answer depend on the machine the suite runs on.
        qunsetenv("HYPRLAND_CONFIG");
        qputenv("XDG_CONFIG_HOME", path(QStringLiteral("xdg-config-home")).toUtf8());
        qputenv("HOME", path(QStringLiteral("home")).toUtf8());
        qputenv("XDG_CONFIG_DIRS", path(QStringLiteral("xdg-config-dirs")).toUtf8());
    }

    [[nodiscard]] QString path(const QString &relative) const
    {
        return m_dir.path() + QLatin1Char('/') + relative;
    }

    // Writes an empty hypr/hyprland.<extension> under base and answers its full path.
    [[nodiscard]] QString writeConfig(const QString &base, const QString &extension) const
    {
        const QString dir = base + QLatin1String("/hypr");
        EXPECT_TRUE(QDir{}.mkpath(dir));
        const QString file = dir + QLatin1String("/hyprland.") + extension;
        QFile handle{file};
        EXPECT_TRUE(handle.open(QIODevice::WriteOnly));
        return file;
    }

private:
    QTemporaryDir m_dir;
};

} // namespace

TEST_F(HyprlandConfigTest, answersLuaWhereNoConfigurationExistsYet)
{
    // What Hyprland would generate for a fresh installation, so what the advice should be in.
    const HyprlandConfig config = hyprlandConfig();
    EXPECT_EQ(config.language, HyprlandConfigLanguage::Lua);
    EXPECT_TRUE(config.path.isEmpty());
    EXPECT_EQ(config.fileName(), QLatin1String("hyprland.lua"));
}

TEST_F(HyprlandConfigTest, answersHyprlangForAUserWhoStillHasAConfFile)
{
    const QString file = writeConfig(path(QStringLiteral("xdg-config-home")), QStringLiteral("conf"));
    const HyprlandConfig config = hyprlandConfig();
    EXPECT_EQ(config.language, HyprlandConfigLanguage::Hyprlang);
    EXPECT_EQ(config.path, file);
    EXPECT_EQ(config.fileName(), QLatin1String("hyprland.conf"));
}

TEST_F(HyprlandConfigTest, prefersTheLuaFileWhereBothExistInOneDirectory)
{
    EXPECT_FALSE(writeConfig(path(QStringLiteral("xdg-config-home")), QStringLiteral("conf")).isEmpty());
    const QString lua = writeConfig(path(QStringLiteral("xdg-config-home")), QStringLiteral("lua"));
    const HyprlandConfig config = hyprlandConfig();
    EXPECT_EQ(config.language, HyprlandConfigLanguage::Lua);
    EXPECT_EQ(config.path, lua);
}

TEST_F(HyprlandConfigTest, prefersALuaFileInALowerDirectoryOverAConfFileInAHigherOne)
{
    // The whole search runs for "lua" before it runs for "conf", rather than each directory
    // being asked about both extensions. A per-directory search would answer hyprlang here,
    // and so would tell a user with a Lua configuration to edit a file Hyprland never reads.
    EXPECT_FALSE(writeConfig(path(QStringLiteral("xdg-config-home")), QStringLiteral("conf")).isEmpty());
    const QString lua = writeConfig(path(QStringLiteral("xdg-config-dirs")), QStringLiteral("lua"));
    const HyprlandConfig config = hyprlandConfig();
    EXPECT_EQ(config.language, HyprlandConfigLanguage::Lua);
    EXPECT_EQ(config.path, lua);
}

TEST_F(HyprlandConfigTest, fallsBackToHomeConfigWhereXdgConfigHomeIsUnset)
{
    qunsetenv("XDG_CONFIG_HOME");
    const QString file = writeConfig(path(QStringLiteral("home/.config")), QStringLiteral("conf"));
    const HyprlandConfig config = hyprlandConfig();
    EXPECT_EQ(config.language, HyprlandConfigLanguage::Hyprlang);
    EXPECT_EQ(config.path, file);
}

TEST_F(HyprlandConfigTest, takesHyprlandConfigOverEveryDirectorySearch)
{
    // The variable names a file directly, and its extension decides on its own -- the file need
    // not exist for Hyprland to pick a manager for it.
    EXPECT_FALSE(writeConfig(path(QStringLiteral("xdg-config-home")), QStringLiteral("lua")).isEmpty());
    qputenv("HYPRLAND_CONFIG", path(QStringLiteral("elsewhere/my.conf")).toUtf8());
    const HyprlandConfig config = hyprlandConfig();
    EXPECT_EQ(config.language, HyprlandConfigLanguage::Hyprlang);
    EXPECT_EQ(config.path, path(QStringLiteral("elsewhere/my.conf")));
}

TEST_F(HyprlandConfigTest, readsAnExplicitPathWithNoRecognisedExtensionAsHyprlang)
{
    // Hyprland tests for ".lua" and treats everything else as the legacy language, rather than
    // requiring ".conf" (src/config/ConfigManager.cpp:45).
    qputenv("HYPRLAND_CONFIG", path(QStringLiteral("elsewhere/rice")).toUtf8());
    EXPECT_EQ(hyprlandConfig().language, HyprlandConfigLanguage::Hyprlang);
}

TEST(HyprlandRuleTest, writesTheLayerRuleTheCompositorAccepts)
{
    // Both forms name the effect no_screen_share and the namespace popup::PopupWindow assigns.
    // The hyprlang keyword takes `key value` fields separated by commas, with a match property
    // carrying a `match:` prefix; the two-argument `noscreenshare, marupop-popup` form this
    // replaced names no field that exists.
    EXPECT_EQ(noScreenShareRule(HyprlandConfigLanguage::Lua),
              QLatin1String(R"(hl.layer_rule({ match = { namespace = "marupop-popup" }, no_screen_share = true }))"));
    EXPECT_EQ(noScreenShareRule(HyprlandConfigLanguage::Hyprlang),
              QLatin1String("layerrule = match:namespace marupop-popup, no_screen_share 1"));
}

TEST(HyprlandRuleTest, writesTheScreencopyPermissionForAnAbsoluteBinary)
{
    EXPECT_EQ(screencopyPermissionRule(HyprlandConfigLanguage::Lua, QStringLiteral("/usr/bin/marupop")),
              QLatin1String(R"(hl.permission({ binary = "/usr/bin/marupop", type = "screencopy", mode = "allow" }))"));
    EXPECT_EQ(screencopyPermissionRule(HyprlandConfigLanguage::Hyprlang, QStringLiteral("/usr/bin/marupop")),
              QLatin1String("permission = /usr/bin/marupop, screencopy, allow"));
}
