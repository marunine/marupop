# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# The Wayland globals Qt's platform plugin does not bind. Everything here lives in maru::wl.
#
# The generated client bindings for the three vendored protocols are added by
# src/CMakeLists.txt rather than by this file. A wayland-scanner invocation is an
# add_custom_command(), whose OUTPUT can only be consumed by a target defined in the same
# directory, and marupop_lib is defined in src/. Generating them here would leave
# marupop_lib_autogen looking for a header no rule in its own directory produces.
target_sources(marupop_lib PRIVATE
    registry.cpp
    registry.h
    shmbuffer.cpp
    shmbuffer.h)

# Wayland::Client carries libwayland-client, whose wl_display, wl_registry and wl_buffer appear
# in wayland/registry.h and wayland/shmbuffer.h. This is the only place marupop_lib is linked
# against it, and PUBLIC because those types are in headers the tests include.
target_link_libraries(marupop_lib PUBLIC Wayland::Client)
