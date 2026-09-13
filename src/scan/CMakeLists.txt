# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# The scan loop: pointer position in, dictionary response out. Everything here lives in the
# maru::scan namespace.
target_sources(marupop_lib PRIVATE
    hitcontext.h
    scancache.cpp
    scancache.h
    scancontroller.cpp
    scancontroller.h
    throttle.cpp
    throttle.h
    wordcopier.cpp
    wordcopier.h)

# Qt6::Concurrent supplies QtConcurrent::run(), which scancontroller.cpp runs the dictionary
# lookup on so a slow query never holds the pointer path. KF6::GuiAddons supplies
# KSystemClipboard, which wordcopier.cpp writes the copy-word hotkey's string through because a
# Wayland client without keyboard focus may not touch QClipboard. Both are already on
# marupop_lib's PUBLIC link line from src/CMakeLists.txt; they are named here as well so the
# dependency is recorded where the source that needs it is.
target_link_libraries(marupop_lib PRIVATE Qt6::Concurrent KF6::GuiAddons)
