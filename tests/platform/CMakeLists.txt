# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# Session detection and the backend factory.
#
# session_test needs no Q*Application: its two live probes are a session-bus name lookup, which
# QDBusConnection answers without one, and a stat(2).
maru_add_gtest(session_test session_test.cpp)

# backend_test builds every backend, and three of the four implementations need a
# QGuiApplication: HotkeyRegistry reads QGuiApplication::desktopFileName(), WlrFrameSource and
# WlrShortcuts read the Wayland registry, and KWinScriptRelay writes a script package under the
# suite's own HOME.
maru_add_gtest_app(backend_test backend_test.cpp)

# The four wlroots-family backends against a real Hyprland. Every case is gated on
# MARUPOP_LIVE_HYPRLAND=1, so the plain registration skips the whole suite; the registration
# below runs it inside the nested session hyprland-session.sh starts, which is what exports the
# variable. This is the only coverage the three protocol clients have of their wire paths.
maru_add_gtest_app(hyprlandlive_test hyprlandlive_test.cpp)
target_link_libraries(hyprlandlive_test PRIVATE marupop_testsupport)
maru_add_hyprland_test(hyprlandlive_test_nested BINARY hyprlandlive_test)

# The same suite with ecosystem:enforce_permissions turned on and the two permission rules
# written, which is the branch a default Hyprland session never reaches: the option is false by
# default (Hyprland 0.56.0, src/config/values/ConfigValues.cpp:714).
maru_add_hyprland_test(hyprlandlive_test_permission_nested BINARY hyprlandlive_test PERMISSION)

# Enforcement on and no rule, so every screencopy permission stays pending and the compositor
# answers a copy with neither ready nor failed. Only the watchdog case runs there; the other
# cases skip themselves on the variable the harness exports.
maru_add_hyprland_test(hyprlandlive_test_pending_nested BINARY hyprlandlive_test ENFORCE_ONLY)

# Two outputs, which is what covers the output-local coordinates
# zwlr_screencopy_v1.capture_output_region takes and the lattice capture::wlrTileFor() anchors at
# each output's own origin. A single-output session puts both origins at 0,0 and would pass with
# either one wrong.
maru_add_hyprland_test(hyprlandlive_test_dual_nested BINARY hyprlandlive_test OUTPUTS 2)
