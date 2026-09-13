// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The defaults marupopsettings.kcfg declares, and the enum bridge in core/settings.h. The
// bridge is a static_cast in both directions, so what the suite covers is the ordering
// agreement between the kcfg choice lists and core/enums.h: a choice inserted in one file and
// not the other moves a value here.
#include "core/enums.h"
#include "core/paths.h"
#include "core/settings.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QSize>

#include <gtest/gtest.h>

using namespace maru;

namespace
{

// Restores every entry to its declared default, so an assertion on a default holds whatever a
// previous case wrote into marupoprc.
void resetSettings()
{
    PopSettings::self()->setDefaults();
    PopSettings::self()->save();
}

} // namespace

TEST(SettingsTest, carriesTheDeclaredDefaults)
{
    resetSettings();

    EXPECT_TRUE(PopSettings::showTrayIcon());
    EXPECT_FALSE(PopSettings::autostart());
    EXPECT_TRUE(PopSettings::scanningEnabled());
    EXPECT_FALSE(PopSettings::firstRunCompleted());

    EXPECT_EQ(PopSettings::cursorMoveThrottleMs(), 200);
    EXPECT_EQ(PopSettings::periodicPollIntervalMs(), 2000);
    EXPECT_TRUE(PopSettings::progressiveScanArea());

    EXPECT_EQ(PopSettings::meikiIntraOpThreads(), 6);
    EXPECT_DOUBLE_EQ(PopSettings::meikiDetectionThreshold(), 0.5);
    EXPECT_DOUBLE_EQ(PopSettings::meikiRecognitionThreshold(), 0.1);
    EXPECT_EQ(PopSettings::screenAiResourcesDir(), QStringLiteral("~/.config/screen_ai/resources"));

    EXPECT_EQ(PopSettings::maxLookupLength(), 41);
    EXPECT_EQ(PopSettings::maxResults(), 10);

    EXPECT_TRUE(PopSettings::showFrequency());
    EXPECT_TRUE(PopSettings::compactMode());
    EXPECT_FALSE(PopSettings::showAllGlosses());

    EXPECT_EQ(PopSettings::colorBackground(), QColor{QStringLiteral("#2E2E2E")});
    EXPECT_EQ(PopSettings::colorHighlightReading(), QColor{QStringLiteral("#90EE90")});
    EXPECT_EQ(PopSettings::backgroundOpacity(), 245);
    EXPECT_EQ(PopSettings::popupCursorOffset(), 15);
}

TEST(SettingsTest, readsTheScanLadderAsTwoSizes)
{
    resetSettings();
    EXPECT_EQ(settings::initialScanSize(), QSize(480, 270));
    EXPECT_EQ(settings::maxScanSize(), QSize(1600, 900));
}

TEST(SettingsTest, bridgesEveryEnumInBothDirections)
{
    resetSettings();

    EXPECT_EQ(settings::ocrEngine(), OcrEngine::Automatic);
    settings::setOcrEngine(OcrEngine::ScreenAi);
    EXPECT_EQ(settings::ocrEngine(), OcrEngine::ScreenAi);
    EXPECT_EQ(PopSettings::ocrEngine(), PopSettings::EnumOcrEngine::ScreenAi);

    EXPECT_EQ(settings::lookupCategory(), LookupCategory::All);
    settings::setLookupCategory(LookupCategory::Kanji);
    EXPECT_EQ(settings::lookupCategory(), LookupCategory::Kanji);
    EXPECT_EQ(PopSettings::lookupCategory(), PopSettings::EnumLookupCategory::Kanji);

    EXPECT_EQ(settings::copyWordMode(), CopyWordMode::MatchedText);
    settings::setCopyWordMode(CopyWordMode::Reading);
    EXPECT_EQ(settings::copyWordMode(), CopyWordMode::Reading);
    EXPECT_EQ(PopSettings::copyWordMode(), PopSettings::EnumCopyWordMode::Reading);

    EXPECT_EQ(settings::themePreset(), ThemePreset::Nazeka);
    settings::setThemePreset(ThemePreset::Custom);
    EXPECT_EQ(settings::themePreset(), ThemePreset::Custom);
    EXPECT_EQ(PopSettings::themePreset(), PopSettings::EnumThemePreset::Custom);

    EXPECT_EQ(settings::popupPositionMode(), PopupPositionMode::VisualNovel);
    settings::setPopupPositionMode(PopupPositionMode::FlipHorizontally);
    EXPECT_EQ(settings::popupPositionMode(), PopupPositionMode::FlipHorizontally);
    EXPECT_EQ(PopSettings::popupPositionMode(), PopSettings::EnumPopupPositionMode::FlipHorizontally);
}

TEST(SettingsTest, fallsBackToTheModelsDirectoryForAnEmptySetting)
{
    resetSettings();
    EXPECT_EQ(settings::modelDirectory(), paths::modelsDir());

    PopSettings::setModelDirectory(QStringLiteral("~/models"));
    EXPECT_EQ(settings::modelDirectory(), QDir::homePath() + QStringLiteral("/models"));
}

TEST(SettingsTest, writesTheScanningStateThroughOnAChange)
{
    resetSettings();
    ASSERT_TRUE(PopSettings::scanningEnabled());

    settings::persistScanningEnabled(false);
    EXPECT_FALSE(PopSettings::scanningEnabled());
    // Read back from the file rather than from the singleton's cache, which is what "persisted"
    // means for a state a restart has to resume.
    PopSettings::self()->load();
    EXPECT_FALSE(PopSettings::scanningEnabled());

    settings::persistScanningEnabled(true);
    PopSettings::self()->load();
    EXPECT_TRUE(PopSettings::scanningEnabled());
}

int main(int argc, char **argv)
{
    QApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
