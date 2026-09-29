# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
target_sources(marupop_lib PRIVATE
    messagewindow.cpp
    messagewindow.h
    registrywatcher.cpp
    registrywatcher.h
    window.cpp
    window.h)

target_link_libraries(marupop_lib PRIVATE user32 advapi32)
