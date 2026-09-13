// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Enums shared across modules. The settings schema (core/marupopsettings.kcfg) mirrors these
// choice lists in the same declaration order, so settings.h bridges them with static_cast;
// keep the two in sync.
#pragma once

namespace maru
{

// Which text-recognition backend ocr::OcrService runs. Automatic prefers MeikiOcr and falls
// back to ScreenAi when the meikiocr model files are absent, which is the state of a first run
// before ocr::ModelStore has downloaded them.
enum class OcrEngine
{
    Automatic,
    MeikiOcr,
    ScreenAi,
};

// Which dictionary types a lookup consults. The value narrows the set of opened stores a
// lookup::Engine queries; All queries every enabled dictionary.
enum class LookupCategory
{
    All,
    Word,
    Name,
    Kanji,
};

// Which string the copy-word hotkey writes to the clipboard. MatchedText is the raw source
// characters the popup highlights, before deconjugation and before normalization.
enum class CopyWordMode
{
    MatchedText,
    Headword,
    Reading,
};

// Popup color and font presets. Nazeka and the three that follow are fixed value sets;
// Custom reads the ColorBackground, ColorForeground, ColorHighlightWord and
// ColorHighlightReading entries instead.
enum class ThemePreset
{
    Nazeka,
    CelestialIndigo,
    NeutralSlate,
    Academic,
    Custom,
};

// How popup::PopupWindow places the popup against the pointer when the preferred placement
// would leave the screen. VisualNovel interpolates the horizontal anchor across the screen
// width rather than flipping at one threshold.
enum class PopupPositionMode
{
    FlipBoth,
    FlipVertically,
    FlipHorizontally,
    VisualNovel,
};

} // namespace maru
