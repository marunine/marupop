<!-- SPDX-FileCopyrightText: 2026 marunine -->
<!-- SPDX-License-Identifier: LGPL-3.0-only -->
# Vendored Wayland protocol definitions

The three XML files here are copied verbatim from their upstream trees.
`src/CMakeLists.txt` runs `ecm_add_wayland_client_protocol()` over each one and links
the generated client bindings into `marupop_lib`. Each file carries its own licence in its
`<copyright>` element, and NOTICE records the same attribution.

| File | Upstream tree | Commit | Licence |
|---|---|---|---|
| `wlr-screencopy-unstable-v1.xml` | `github.com/swaywm/wlroots`, `protocol/` | [728bd33c12459f58312a31eae9acd947488515f1](https://github.com/swaywm/wlroots/tree/728bd33c12459f58312a31eae9acd947488515f1) | MIT, © 2018 Simon Ser, © 2019 Andri Yngvason |
| `hyprland-lock-notify-v1.xml` | `github.com/hyprwm/hyprland-protocols`, `protocols/` | [1cb6db5fd6bb8aee419f4457402fa18293ace917](https://github.com/hyprwm/hyprland-protocols/tree/1cb6db5fd6bb8aee419f4457402fa18293ace917) | BSD-3-Clause, © 2025 Maximilian Seidler |
| `hyprland-global-shortcuts-v1.xml` | `github.com/hyprwm/hyprland-protocols`, `protocols/` | [1cb6db5fd6bb8aee419f4457402fa18293ace917](https://github.com/hyprwm/hyprland-protocols/tree/1cb6db5fd6bb8aee419f4457402fa18293ace917) | BSD-3-Clause, © 2022 Vaxry |

Copying rather than depending on `wayland-protocols` and `hyprland-protocols` at build time has
two reasons. `zwlr_screencopy_unstable_v1` belongs to neither package: wlroots ships it inside
its own source tree, so a distribution that carries no wlroots checkout carries no copy of it.
Pinning the other two keeps the generated bindings stable against a `hyprland-protocols` release
that adds an interface version.

Updating one file means re-copying it from the tree named above and recording the new commit in
this table.
