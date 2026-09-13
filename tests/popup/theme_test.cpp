// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The four theme presets and the round trip through the PopupAppearance group of marupoprc.
#include "core/enums.h"
#include "core/settings.h"
#include "popup/renderoptions.h"
#include "popup/theme.h"

#include <QApplication>

#include <gtest/gtest.h>

using namespace maru;
using namespace maru::popup;

namespace
{

void resetSettings()
{
    PopSettings::self()->setDefaults();
    PopSettings::self()->save();
}

QColor named(const char *name)
{
    return QColor{QLatin1StringView{name}};
}

} // namespace

TEST(ThemeTest, carriesMeikipopsFourPresets)
{
    const Theme nazeka = presetTheme(ThemePreset::Nazeka);
    EXPECT_EQ(nazeka.background, named("#2E2E2E"));
    EXPECT_EQ(nazeka.foreground, named("#F0F0F0"));
    EXPECT_EQ(nazeka.highlightWord, named("#88D8FF"));
    EXPECT_EQ(nazeka.highlightReading, named("#90EE90"));

    const Theme indigo = presetTheme(ThemePreset::CelestialIndigo);
    EXPECT_EQ(indigo.background, named("#281E50"));
    EXPECT_EQ(indigo.foreground, named("#EAEFF5"));
    EXPECT_EQ(indigo.highlightWord, named("#D4C58A"));
    EXPECT_EQ(indigo.highlightReading, named("#B5A2D4"));

    const Theme slate = presetTheme(ThemePreset::NeutralSlate);
    EXPECT_EQ(slate.background, named("#5D5C5B"));
    EXPECT_EQ(slate.foreground, named("#EFEBE8"));
    EXPECT_EQ(slate.highlightWord, named("#A3B8A3"));
    EXPECT_EQ(slate.highlightReading, named("#A3B8A3"));

    const Theme academic = presetTheme(ThemePreset::Academic);
    EXPECT_EQ(academic.background, named("#FDFBF7"));
    EXPECT_EQ(academic.foreground, named("#212121"));
    EXPECT_EQ(academic.highlightWord, named("#8C2121"));
    EXPECT_EQ(academic.highlightReading, named("#005A9C"));

    // Every preset ships the same opacity and the same border.
    for (const ThemePreset preset :
         {ThemePreset::Nazeka, ThemePreset::CelestialIndigo, ThemePreset::NeutralSlate, ThemePreset::Academic}) {
        EXPECT_EQ(presetTheme(preset).backgroundOpacity, 245);
        EXPECT_EQ(presetTheme(preset).borderColor, named("#555555"));
    }
}

TEST(ThemeTest, resolvesCustomToTheNazekaColors)
{
    // ThemePreset::Custom names no color set; themeFromSettings() is what reads the entries.
    EXPECT_EQ(presetTheme(ThemePreset::Custom).background, presetTheme(ThemePreset::Nazeka).background);
}

TEST(ThemeTest, readsTheDeclaredDefaultsOutOfTheSettings)
{
    resetSettings();
    const Theme theme = themeFromSettings();
    EXPECT_EQ(theme.background, named("#2E2E2E"));
    EXPECT_EQ(theme.backgroundOpacity, 245);
    EXPECT_TRUE(theme.fontFamily.isEmpty());
    EXPECT_EQ(theme.headerPt, 18);
    EXPECT_EQ(theme.definitionPt, 14);
    EXPECT_EQ(theme.lineHeightPercent, 100);
    EXPECT_EQ(theme.entrySpacing, 0);
    EXPECT_EQ(theme.cornerRadius, 8);
    EXPECT_EQ(theme.maxWidth, 600);
    EXPECT_EQ(theme.maxHeight, 700);
    EXPECT_EQ(theme.cursorOffset, 15);
    EXPECT_EQ(theme.fadeMs, 120);
}

TEST(ThemeTest, takesTheColorsFromThePresetWhileOneIsSelected)
{
    resetSettings();
    // A color entry left over from an earlier Custom theme is ignored while a preset is
    // selected, so a preset always renders the colors it names.
    PopSettings::setColorBackground(named("#010203"));
    settings::setThemePreset(ThemePreset::Academic);
    EXPECT_EQ(themeFromSettings().background, named("#FDFBF7"));
}

TEST(ThemeTest, takesTheColorsFromTheEntriesUnderCustom)
{
    resetSettings();
    settings::setThemePreset(ThemePreset::Custom);
    PopSettings::setColorBackground(named("#010203"));
    PopSettings::setColorForeground(named("#040506"));
    PopSettings::setColorHighlightWord(named("#070809"));
    PopSettings::setColorHighlightReading(named("#0A0B0C"));

    const Theme theme = themeFromSettings();
    EXPECT_EQ(theme.background, named("#010203"));
    EXPECT_EQ(theme.foreground, named("#040506"));
    EXPECT_EQ(theme.highlightWord, named("#070809"));
    EXPECT_EQ(theme.highlightReading, named("#0A0B0C"));
}

TEST(ThemeTest, applyPresetToSettingsWritesTheColorsThrough)
{
    resetSettings();
    applyPresetToSettings(ThemePreset::CelestialIndigo);

    EXPECT_EQ(settings::themePreset(), ThemePreset::CelestialIndigo);
    EXPECT_EQ(PopSettings::colorBackground(), named("#281E50"));
    EXPECT_EQ(PopSettings::colorForeground(), named("#EAEFF5"));
    EXPECT_EQ(PopSettings::colorHighlightWord(), named("#D4C58A"));
    EXPECT_EQ(PopSettings::colorHighlightReading(), named("#B5A2D4"));
    EXPECT_EQ(PopSettings::backgroundOpacity(), 245);

    // Selecting Custom keeps the four entries the previous preset wrote.
    applyPresetToSettings(ThemePreset::Custom);
    EXPECT_EQ(settings::themePreset(), ThemePreset::Custom);
    EXPECT_EQ(PopSettings::colorBackground(), named("#281E50"));
}

TEST(ThemeTest, carriesTheFontsAndTheGeometryThroughTheSettings)
{
    resetSettings();
    PopSettings::setFontFamily(QStringLiteral("Noto Sans CJK JP"));
    PopSettings::setFontSizeHeader(22);
    PopSettings::setFontSizeDefinitions(16);
    PopSettings::setBackgroundOpacity(200);
    PopSettings::setLineHeight(150);
    PopSettings::setEntrySpacing(9);
    PopSettings::setPopupCornerRadius(12);
    PopSettings::setPopupMaxWidth(800);
    PopSettings::setPopupMaxHeight(900);
    PopSettings::setPopupCursorOffset(24);
    PopSettings::setPopupFadeMs(0);

    const Theme theme = themeFromSettings();
    EXPECT_EQ(theme.fontFamily, QStringLiteral("Noto Sans CJK JP"));
    EXPECT_EQ(theme.headerPt, 22);
    EXPECT_EQ(theme.definitionPt, 16);
    EXPECT_EQ(theme.backgroundOpacity, 200);
    EXPECT_EQ(theme.lineHeightPercent, 150);
    EXPECT_EQ(theme.entrySpacing, 9);
    EXPECT_EQ(theme.cornerRadius, 12);
    EXPECT_EQ(theme.maxWidth, 800);
    EXPECT_EQ(theme.maxHeight, 900);
    EXPECT_EQ(theme.cursorOffset, 24);
    EXPECT_EQ(theme.fadeMs, 0);
}

TEST(ThemeTest, blendsAFractionOfTheForegroundOverTheBackground)
{
    const QColor white{255, 255, 255};
    const QColor black{0, 0, 0};
    EXPECT_EQ(blend(white, black, 1.0), white);
    EXPECT_EQ(blend(white, black, 0.0), black);
    EXPECT_EQ(blend(white, black, 0.5), QColor(128, 128, 128));
    // A fraction outside the range is clamped rather than extrapolated.
    EXPECT_EQ(blend(white, black, 2.0), white);
    EXPECT_EQ(blend(white, black, -1.0), black);
}

TEST(ThemeTest, readsEveryPopupContentEntryIntoTheRenderOptions)
{
    resetSettings();
    const RenderOptions defaults = renderOptionsFromSettings();
    EXPECT_FALSE(defaults.showAllGlosses);
    EXPECT_TRUE(defaults.showDeconjugation);
    EXPECT_FALSE(defaults.showPartOfSpeech);
    EXPECT_FALSE(defaults.showTags);
    EXPECT_TRUE(defaults.showFrequency);
    EXPECT_TRUE(defaults.showKanji);
    EXPECT_TRUE(defaults.showKanjiExamples);
    EXPECT_TRUE(defaults.showKanjiComponents);
    EXPECT_TRUE(defaults.compactMode);
    EXPECT_TRUE(defaults.showAlternativeSpellings);
    EXPECT_TRUE(defaults.showPitchAccent);
    EXPECT_FALSE(defaults.showDictionaryName);
    EXPECT_TRUE(defaults.showOrthographyInfo);
    EXPECT_TRUE(defaults.showStrokeCountAndGrade);

    PopSettings::setShowAllGlosses(true);
    PopSettings::setCompactMode(false);
    PopSettings::setShowKanji(false);
    const RenderOptions changed = renderOptionsFromSettings();
    EXPECT_TRUE(changed.showAllGlosses);
    EXPECT_FALSE(changed.compactMode);
    EXPECT_FALSE(changed.showKanji);
}

int main(int argc, char **argv)
{
    QApplication app{argc, argv};
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
