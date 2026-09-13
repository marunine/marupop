// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
#include "capture/hyprlandconfig.h"

#include "core/layerscope.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace maru::capture
{

namespace
{

// The four bases Hyprutils::Path::findConfig() walks, in its order: $XDG_CONFIG_HOME, then
// $HOME/.config, then each entry of $XDG_CONFIG_DIRS, then /etc/xdg. A relative
// $XDG_CONFIG_HOME or $HOME is skipped rather than resolved against the working directory.
QStringList configBases()
{
    QStringList bases;
    const QString xdgConfigHome = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!xdgConfigHome.isEmpty() && QDir::isAbsolutePath(xdgConfigHome)) {
        bases.append(xdgConfigHome);
    }
    const QString home = qEnvironmentVariable("HOME");
    if (!home.isEmpty() && QDir::isAbsolutePath(home)) {
        bases.append(home + "/.config"_L1);
    }
    const QString xdgConfigDirs = qEnvironmentVariable("XDG_CONFIG_DIRS");
    if (!xdgConfigDirs.isEmpty()) {
        bases.append(xdgConfigDirs.split(u':', Qt::SkipEmptyParts));
    }
    bases.append(u"/etc/xdg"_s);
    return bases;
}

// The first existing hypr/hyprland.<extension> across the bases, or an empty string.
QString findConfig(const QStringList &bases, QLatin1StringView extension)
{
    for (const QString &base : bases) {
        const QString candidate = base + "/hypr/hyprland."_L1 + extension;
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

} // namespace

QString HyprlandConfig::fileName() const
{
    return language == HyprlandConfigLanguage::Lua ? u"hyprland.lua"_s : u"hyprland.conf"_s;
}

HyprlandConfig hyprlandConfig()
{
    // An explicit path decides on its own, and by its extension alone: Hyprland treats anything
    // that is not ".lua" as hyprlang, rather than requiring ".conf".
    const QString explicitPath = qEnvironmentVariable("HYPRLAND_CONFIG");
    if (!explicitPath.isEmpty()) {
        const bool lua = explicitPath.endsWith(".lua"_L1, Qt::CaseInsensitive);
        return {.language = lua ? HyprlandConfigLanguage::Lua : HyprlandConfigLanguage::Hyprlang, .path = explicitPath};
    }

    // Lua is searched across every base before hyprlang is searched at all, so a hyprland.lua in
    // $XDG_CONFIG_HOME wins over a hyprland.conf in /etc/xdg and a hyprland.conf in
    // $XDG_CONFIG_HOME wins over nothing.
    const QStringList bases = configBases();
    if (const QString lua = findConfig(bases, "lua"_L1); !lua.isEmpty()) {
        return {.language = HyprlandConfigLanguage::Lua, .path = lua};
    }
    if (const QString conf = findConfig(bases, "conf"_L1); !conf.isEmpty()) {
        return {.language = HyprlandConfigLanguage::Hyprlang, .path = conf};
    }
    // Neither exists. Hyprland generates a Lua one from example/hyprland.lua, so that is the
    // language the advice should be in.
    return {.language = HyprlandConfigLanguage::Lua, .path = {}};
}

QString noScreenShareRule(HyprlandConfigLanguage language)
{
    if (language == HyprlandConfigLanguage::Lua) {
        return "hl.layer_rule({ match = { namespace = \""_L1 + QString{popupLayerScope} +
               "\" }, no_screen_share = true })"_L1;
    }
    // The hyprlang form uses named fields and match: properties. Keep it in sync with the
    // supported compositor syntax. The nested harness exercises Lua only; this legacy form
    // needs a manual configerrors/capture check when changed.
    return "layerrule = match:namespace "_L1 + QString{popupLayerScope} + ", no_screen_share 1"_L1;
}

QString globalShortcutBind(HyprlandConfigLanguage language, const QStringList &keyTokens, const QString &selector)
{
    if (keyTokens.isEmpty()) {
        return {};
    }
    if (language == HyprlandConfigLanguage::Lua) {
        return "hl.bind(\""_L1 + keyTokens.join(" + "_L1) + "\", hl.dsp.global(\""_L1 + selector + "\"))"_L1;
    }
    // The hyprlang keyword takes the modifiers and the key as two comma-separated fields, with
    // the modifiers space-separated inside the first.
    const QString modifiers = QStringList{keyTokens.first(keyTokens.size() - 1)}.join(QLatin1Char(' '));
    return "bind = "_L1 + modifiers + ", "_L1 + keyTokens.last() + ", global, "_L1 + selector;
}

QString screencopyPermissionRule(HyprlandConfigLanguage language, const QString &binaryPath)
{
    if (language == HyprlandConfigLanguage::Lua) {
        return "hl.permission({ binary = \""_L1 + binaryPath + R"(", type = "screencopy", mode = "allow" }))"_L1;
    }
    return "permission = "_L1 + binaryPath + ", screencopy, allow"_L1;
}

QString enforcePermissionsOption(HyprlandConfigLanguage language)
{
    return language == HyprlandConfigLanguage::Lua ? u"ecosystem.enforce_permissions"_s
                                                   : u"ecosystem:enforce_permissions"_s;
}

} // namespace maru::capture
