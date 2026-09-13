// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Which compositor the process is talking to, which is what selects the pointer source, the
// pixel source, the lock watcher and the shortcut registry.
//
// The four capabilities marupop needs beyond plain Wayland are reached three different ways per
// compositor family, and none of them is negotiable at
// run time by a protocol handshake alone: KDE's are D-Bus interfaces on org.kde.KWin, and
// Hyprland's are a UNIX socket plus two Wayland globals. Detection therefore reads the session
// rather than probing every mechanism in turn.
#pragma once

#include <QString>

namespace maru::platform
{

enum class Session
{
    // No compositor was recognized. Every backend answers unavailable, the popup falls back to
    // a plain toplevel, and the application reports what is missing.
    Unknown,
    // org.kde.KWin owns its name on the session bus.
    KdePlasma,
    // HYPRLAND_INSTANCE_SIGNATURE names an existing socket1 directory.
    Hyprland,
    // zwlr_screencopy_manager_v1 is in the Wayland registry and neither of the two above holds.
    Wlroots,
};

// The session this process is running in. Evaluated once and cached, because the answer cannot
// change while the process lives: a compositor restart takes the Wayland connection with it.
//
// MARUPOP_PLATFORM overrides the answer with one of "kde", "hyprland", "wlroots" or "unknown",
// which is what lets a suite drive a backend the host session does not offer and what lets a
// user work around a detection this function gets wrong. An unrecognized value is reported
// through qCWarning(logPlatform) and ignored.
[[nodiscard]] Session detect();

// Re-runs the detection, discarding the cached answer. For a test that changes the environment
// between cases; the application calls detect() alone.
[[nodiscard]] Session redetect();

// The untranslated identifier, which is the value MARUPOP_PLATFORM takes and the string the
// diagnostics print: "kde", "hyprland", "wlroots", "unknown".
[[nodiscard]] QString sessionId(Session session);
// The parse of sessionId(), for MARUPOP_PLATFORM and for a test. Unknown for an unrecognized
// string, with recognized set to false.
[[nodiscard]] Session parseSessionId(const QString &id, bool *recognized = nullptr);

// The name shown in the settings dialog and in --check-authorization: "KDE Plasma",
// "Hyprland", "wlroots", "unrecognized".
[[nodiscard]] QString sessionName(Session session);

// True for a session whose pixel source composites marupop's own popup into the captured
// region. The wlroots capture path sets this flag. Popup placement avoids the resolved
// paragraph and the hit test rejects the popup rectangle
// while this is true.
[[nodiscard]] bool capturesOwnWindows(Session session);

} // namespace maru::platform
