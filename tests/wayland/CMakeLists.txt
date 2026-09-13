# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# The Wayland globals Qt's platform plugin does not bind.
#
# Only the parts that need no compositor are here. wl::Registry binds against a live wl_display
# and wl::ShmBuffer::reset() needs a wl_shm, so both are covered by
# tests/platform/hyprlandlive_test.cpp instead, behind MARUPOP_LIVE_HYPRLAND. What is covered
# here is the wl_shm to QImage format mapping, which needs neither and which fails silently: an
# image built with the wrong format has the right size, the right stride and a plausible hash.
#
# A QImage constructor needs no QGuiApplication, so this is the plain form.
maru_add_gtest(shmbuffer_test shmbuffer_test.cpp)
