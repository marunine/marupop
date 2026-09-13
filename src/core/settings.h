// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Thin bridge between the generated PopSettings singleton (KConfigXT) and the plain enums used
// throughout the code. The kcfg choice lists mirror core/enums.h declaration order, so
// conversions are static_casts.
#pragma once

#include "core/enums.h"
#include "marupopsettings.h"

#include <QSize>
#include <QString>

namespace maru::settings
{

inline OcrEngine ocrEngine()
{
    return static_cast<OcrEngine>(PopSettings::ocrEngine());
}

inline void setOcrEngine(OcrEngine engine)
{
    PopSettings::setOcrEngine(static_cast<PopSettings::EnumOcrEngine::type>(engine));
}

inline LookupCategory lookupCategory()
{
    return static_cast<LookupCategory>(PopSettings::lookupCategory());
}

inline void setLookupCategory(LookupCategory category)
{
    PopSettings::setLookupCategory(static_cast<PopSettings::EnumLookupCategory::type>(category));
}

inline CopyWordMode copyWordMode()
{
    return static_cast<CopyWordMode>(PopSettings::copyWordMode());
}

inline void setCopyWordMode(CopyWordMode mode)
{
    PopSettings::setCopyWordMode(static_cast<PopSettings::EnumCopyWordMode::type>(mode));
}

inline ThemePreset themePreset()
{
    return static_cast<ThemePreset>(PopSettings::themePreset());
}

inline void setThemePreset(ThemePreset preset)
{
    PopSettings::setThemePreset(static_cast<PopSettings::EnumThemePreset::type>(preset));
}

inline PopupPositionMode popupPositionMode()
{
    return static_cast<PopupPositionMode>(PopSettings::popupPositionMode());
}

inline void setPopupPositionMode(PopupPositionMode mode)
{
    PopSettings::setPopupPositionMode(static_cast<PopSettings::EnumPopupPositionMode::type>(mode));
}

// The first and the last rectangle of the progressive scan ladder, in logical pixels.
[[nodiscard]] QSize initialScanSize();
[[nodiscard]] QSize maxScanSize();

// Directory holding the meikiocr ONNX models: the configured ModelDirectory with ~ and $VAR
// expanded, or paths::modelsDir() when that entry is empty.
[[nodiscard]] QString modelDirectory();

// Directory holding libchromescreenai.so and its model files, with ~ and $VAR expanded.
[[nodiscard]] QString screenAiResourcesDir();

// Writes the scanning state through, so a restart resumes what the user left. Nothing is
// written when the stored value already matches.
void persistScanningEnabled(bool enabled);

} // namespace maru::settings
