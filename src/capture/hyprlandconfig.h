// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Which of Hyprland's two configuration languages the session is written in, and the layer-rule
// line to hand the user in that language.
#pragma once

#include <QString>
#include <QStringList>

namespace maru::capture
{

// Hyprland reads either a Lua configuration or a hyprlang one, and the two spell the same rule
// differently. Lua is the one the wiki documents and the one a new installation is given;
// hyprlang is the older form, still read by Hyprland 0.56.2 and already removed from upstream
// main, so it will stop working in the release after it.
enum class HyprlandConfigLanguage
{
    Lua,
    Hyprlang,
};

struct HyprlandConfig
{
    HyprlandConfigLanguage language = HyprlandConfigLanguage::Lua;
    // The file the language was decided from. Empty where no configuration file exists yet, in
    // which case the language is Lua, because that is what Hyprland would generate.
    QString path;

    // The file name alone -- "hyprland.lua" or "hyprland.conf" -- for the sentence that
    // introduces the line. Answered from the language rather than from the path, so it is
    // usable where no file exists.
    [[nodiscard]] QString fileName() const;
};

// Repeats Hyprland's own choice of configuration file, so the advice names the file the user
// actually has. This is a best-effort discovery heuristic: HYPRLAND_CONFIG if set, whose
// extension decides on its own; otherwise the first existing hypr/hyprland.lua across
// $XDG_CONFIG_HOME, $HOME/.config, each of $XDG_CONFIG_DIRS and /etc/xdg; otherwise the first
// existing hypr/hyprland.conf across the same four; otherwise Lua.
//
// One case cannot be reproduced from outside the compositor: a session started as
// `Hyprland --config <path>` takes that path and leaves no trace of it in the environment. A
// user who does that and whose explicit file is in the other language is told about the wrong
// one, which is why the caller names the file it decided on rather than only the line.
[[nodiscard]] HyprlandConfig hyprlandConfig();

// The rule that keeps the popup out of a screen copy, written for the language in hand. The
// effect is spelled no_screen_share in both; what differs is the syntax around it.
[[nodiscard]] QString noScreenShareRule(HyprlandConfigLanguage language);

// The `global` key binding for one shortcut. keyTokens are the modifiers followed by the key, in
// Hyprland's own spelling -- {"SUPER", "ALT", "J"} -- because the two languages join them
// differently: hyprlang separates the key from the modifiers with a comma, Lua writes one
// plus-separated string. selector is the `<app_id>:<id>` the compositor matches the registered
// shortcut by.
[[nodiscard]] QString
globalShortcutBind(HyprlandConfigLanguage language, const QStringList &keyTokens, const QString &selector);

// The screencopy permission for binaryPath, which is needed only where the user turned
// permission enforcement on. binaryPath has to be absolute: Hyprland matches the rule against
// /proc/<pid>/exe by exact string or RE2 full match.
[[nodiscard]] QString screencopyPermissionRule(HyprlandConfigLanguage language, const QString &binaryPath);

// The name of the permission-enforcement option as the language in hand spells it. hyprlang
// separates a section from its key with a colon and Lua nests tables, so the same option reads
// `ecosystem:enforce_permissions` in one and `ecosystem.enforce_permissions` in the other.
[[nodiscard]] QString enforcePermissionsOption(HyprlandConfigLanguage language);

} // namespace maru::capture
