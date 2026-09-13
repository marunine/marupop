// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The colors, the fonts and the geometry of the popup card, resolved from the PopupAppearance
// group of marupoprc.
#pragma once

#include "core/enums.h"

#include <QColor>
#include <QString>

class QPainter;
class QRectF;

namespace maru::popup
{

// Everything renderHtml(), PopupWindow and PopupPreview read about appearance. The four color
// fields follow meikipop's Theme schema; the four tag colors and titleColor follow nazeka's
// generated CSS; see NOTICE for attribution.
struct Theme
{
    // Card background, drawn at backgroundOpacity.
    QColor background{0x2E, 0x2E, 0x2E};
    // Body text.
    QColor foreground{0xF0, 0xF0, 0xF0};
    // Headword and kanji character.
    QColor highlightWord{0x88, 0xD8, 0xFF};
    // Kana reading and pitch-accent contour.
    QColor highlightReading{0x90, 0xEE, 0x90};
    // Alpha of the card background, on the 0 to 255 scale.
    int backgroundOpacity = 245;

    // Empty selects QFontDatabase::systemFont(QFontDatabase::GeneralFont).
    QString fontFamily;
    // Headword size, in points. The reading is emitted two points below it.
    int headerPt = 18;
    // Definition size, in points.
    int definitionPt = 14;
    // Line height of every block that sets none of its own, in percent of the font's line
    // spacing. PopupView applies it after setHtml(); 100 leaves every block at single height.
    int lineHeightPercent = 100;
    // Space added between two entries, in logical pixels. renderHtml() puts half of it above the
    // rule and the other half below it.
    int entrySpacing = 0;

    int cornerRadius = 8;
    // meikipop draws the same #555555 border under every one of its four themes.
    QColor borderColor{0x55, 0x55, 0x55};
    // Bounds of the card, in logical pixels.
    int maxWidth = 600;
    int maxHeight = 700;
    // Gap between the pointer and the card, in logical pixels.
    int cursorOffset = 15;
    // Duration of the show and hide fade, in milliseconds. 0 shows and hides at once.
    int fadeMs = 120;

    // Dictionary name and kanji card headings, nazeka's titlecolor.
    QColor titleColor{0xD8, 0xA8, 0x88};
    // Text of a tag pill, nazeka's info_fgcolor.
    QColor tagTextColor{0xCC, 0xCC, 0xCC};
    // Background of a part-of-speech pill, nazeka's info_bgcolor_pos.
    QColor tagPosColor{0x60, 0x60, 0x60};
    // Background of a sense-number pill, nazeka's info_bgcolor_num.
    QColor tagNumberColor{0x40, 0x60, 0x80};
    // Background of a miscellaneous, field or dialect pill, nazeka's info_bgcolor_etc.
    QColor tagMiscColor{0x60, 0x80, 0x40};

    // Distance between the card border and its content, in logical pixels. meikipop uses the
    // same 10 pixels on all four sides.
    int padding = 10;
};

// The four fixed value sets of ThemePreset, with every field outside the four colors and
// backgroundOpacity left at the default marupopsettings.kcfg declares. ThemePreset::Custom has
// no color set of its own and returns the Nazeka colors; themeFromSettings() is what reads the
// ColorBackground, ColorForeground, ColorHighlightWord and ColorHighlightReading entries for
// it.
[[nodiscard]] Theme presetTheme(ThemePreset preset);

// The theme the PopupAppearance group describes. Colors come from presetTheme() unless the
// preset is ThemePreset::Custom, in which case they come from the four color entries; the
// fonts, the geometry and the fade duration always come from the entries.
[[nodiscard]] Theme themeFromSettings();

// Writes the four colors and the opacity of a preset into marupoprc and selects it, so the
// color buttons of the settings dialog show the values the popup will use. Writing
// ThemePreset::Custom selects the preset and leaves the four color entries as they are.
void applyPresetToSettings(ThemePreset preset);

// Fills a rounded rectangle with Theme::background at Theme::backgroundOpacity and strokes a
// one-pixel Theme::borderColor outline inside it. PopupWindow and PopupPreview both draw their
// card with this.
void paintCard(QPainter &painter, const QRectF &rect, const Theme &theme);

// The color a fraction of foreground over background reads as. Qt's rich-text subset carries
// no opacity property, so the renderer emits a mixed color where meikipop emits an opacity.
// fraction 1.0 returns foreground, 0.0 returns background.
[[nodiscard]] QColor blend(const QColor &foreground, const QColor &background, qreal fraction);

} // namespace maru::popup
