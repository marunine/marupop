// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The icon sources are monochrome SVGs whose strokes carry the ColorScheme-Text class. A KDE
// icon theme recolors the ColorScheme-Text class to the panel text color on load.
#pragma once

#include <QIcon>

namespace maru
{

enum class IconBackground
{
    Tray,
    // A window title bar and the task switcher.
    Window,
};

[[nodiscard]] QIcon applicationIcon(bool paused, IconBackground background);

[[nodiscard]] QString applicationIconName(bool paused);

#ifdef Q_OS_WIN
// The key under HKEY_CURRENT_USER whose SystemUsesLightTheme value stores the taskbar mode.
inline constexpr QLatin1StringView taskbarThemeKey{R"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)"};
#endif

} // namespace maru
