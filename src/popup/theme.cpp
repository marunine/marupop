// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "popup/theme.h"

#include "core/settings.h"

#include <QPainter>
#include <QPainterPath>
#include <QRectF>

namespace maru::popup
{

namespace
{

// Color sets based on meikipop's THEMES table, with background opacity 245.
// See NOTICE for upstream attribution.
struct PresetColors
{
    const char *background;
    const char *foreground;
    const char *highlightWord;
    const char *highlightReading;
};

constexpr PresetColors nazekaColors{
    .background = "#2E2E2E", .foreground = "#F0F0F0", .highlightWord = "#88D8FF", .highlightReading = "#90EE90"};
constexpr PresetColors celestialIndigoColors{
    .background = "#281E50", .foreground = "#EAEFF5", .highlightWord = "#D4C58A", .highlightReading = "#B5A2D4"};
constexpr PresetColors neutralSlateColors{
    .background = "#5D5C5B", .foreground = "#EFEBE8", .highlightWord = "#A3B8A3", .highlightReading = "#A3B8A3"};
constexpr PresetColors academicColors{
    .background = "#FDFBF7", .foreground = "#212121", .highlightWord = "#8C2121", .highlightReading = "#005A9C"};

PresetColors colorsFor(ThemePreset preset)
{
    switch (preset) {
    case ThemePreset::Nazeka:
        return nazekaColors;
    case ThemePreset::CelestialIndigo:
        return celestialIndigoColors;
    case ThemePreset::NeutralSlate:
        return neutralSlateColors;
    case ThemePreset::Academic:
        return academicColors;
    case ThemePreset::Custom:
        break;
    }
    return nazekaColors;
}

QColor fromLatin1(const char *name)
{
    return QColor{QLatin1StringView{name}};
}

} // namespace

Theme presetTheme(ThemePreset preset)
{
    const PresetColors colors = colorsFor(preset);
    Theme theme;
    theme.background = fromLatin1(colors.background);
    theme.foreground = fromLatin1(colors.foreground);
    theme.highlightWord = fromLatin1(colors.highlightWord);
    theme.highlightReading = fromLatin1(colors.highlightReading);
    theme.backgroundOpacity = 245;
    return theme;
}

Theme themeFromSettings()
{
    const ThemePreset preset = settings::themePreset();
    Theme theme = presetTheme(preset);
    if (preset == ThemePreset::Custom) {
        theme.background = PopSettings::colorBackground();
        theme.foreground = PopSettings::colorForeground();
        theme.highlightWord = PopSettings::colorHighlightWord();
        theme.highlightReading = PopSettings::colorHighlightReading();
    }
    theme.backgroundOpacity = PopSettings::backgroundOpacity();
    theme.fontFamily = PopSettings::fontFamily();
    theme.headerPt = PopSettings::fontSizeHeader();
    theme.definitionPt = PopSettings::fontSizeDefinitions();
    theme.lineHeightPercent = PopSettings::lineHeight();
    theme.entrySpacing = PopSettings::entrySpacing();
    theme.cornerRadius = PopSettings::popupCornerRadius();
    theme.maxWidth = PopSettings::popupMaxWidth();
    theme.maxHeight = PopSettings::popupMaxHeight();
    theme.cursorOffset = PopSettings::popupCursorOffset();
    theme.fadeMs = PopSettings::popupFadeMs();
    return theme;
}

void applyPresetToSettings(ThemePreset preset)
{
    settings::setThemePreset(preset);
    if (preset == ThemePreset::Custom) {
        return;
    }
    const Theme theme = presetTheme(preset);
    PopSettings::setColorBackground(theme.background);
    PopSettings::setColorForeground(theme.foreground);
    PopSettings::setColorHighlightWord(theme.highlightWord);
    PopSettings::setColorHighlightReading(theme.highlightReading);
    PopSettings::setBackgroundOpacity(theme.backgroundOpacity);
}

void paintCard(QPainter &painter, const QRectF &rect, const Theme &theme)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor fill = theme.background;
    fill.setAlpha(theme.backgroundOpacity);

    // The outline is stroked on the half-pixel grid inside the rectangle, so a one-pixel pen
    // covers exactly the outermost row and column instead of straddling the edge.
    const QRectF outline = rect.adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = theme.cornerRadius;

    painter.setPen(QPen{theme.borderColor, 1.0});
    painter.setBrush(fill);
    painter.drawRoundedRect(outline, radius, radius);
    painter.restore();
}

QColor blend(const QColor &foreground, const QColor &background, qreal fraction)
{
    const qreal weight = qBound(0.0, fraction, 1.0);
    const auto mix = [weight](int front, int back) {
        return qRound((front * weight) + (back * (1.0 - weight)));
    };
    return QColor{mix(foreground.red(), background.red()),
                  mix(foreground.green(), background.green()),
                  mix(foreground.blue(), background.blue())};
}

} // namespace maru::popup
