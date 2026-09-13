// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Hyprland's socket1, the request-response IPC used by hyprctl.
// The socket lives at $XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket.sock.
// For the commands used here, connect, write one command, read to end of stream and close.
#pragma once

#include <QPoint>
#include <QString>

#include <optional>

namespace maru::cursor
{

// The socket1 path for the running Hyprland instance, or an empty string where
// HYPRLAND_INSTANCE_SIGNATURE or XDG_RUNTIME_DIR is unset. Existence is left to the caller,
// because platform::detect() answers on it and a test points the two variables at a fake.
[[nodiscard]] QString hyprSocketPath();

// The socket1 path under a supplied runtime directory and instance signature. The two-argument
// form is what a test drives; hyprSocketPath() reads the two environment variables and calls it.
[[nodiscard]] QString hyprSocketPath(const QString &runtimeDir, const QString &instanceSignature);

// The pointer position in the reply to the `cursorpos` command, which is
// "<x>, <y>" for the plain format and a JSON object with x and y for the `j/` prefix.
// Both forms are accepted, because the plain
// one is two integers and costs no JSON parse per pointer sample. nullopt for a reply of any
// other shape.
//
// The coordinates are Hyprland's global layout space, in logical pixels: cursorPosRequest
// answers Pointer::mgr()->untransformedPosition().floor(), and CXDGOutputProtocol sends
// xdg_output.logical_position as the same CMonitor::m_position those coordinates are relative to. QScreen::geometry()
// is built from that event, so the answer needs no transform to reach Qt's logical global desktop space.
[[nodiscard]] std::optional<QPoint> parseCursorPos(const QByteArray &reply);

} // namespace maru::cursor
