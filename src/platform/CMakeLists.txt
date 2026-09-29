# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# Session detection and the factory that builds the four session-dependent services.
# Everything here lives in the maru::platform namespace.
#
# This module is listed after capture/, cursor/, popup/ and app/ in src/CMakeLists.txt, because
# backend_wayland.cpp and backend_win.cpp name the implementations of the four interfaces and so
# include a header from each of those modules.
target_sources(marupop_lib PRIVATE
    backend.cpp
    backend.h
    desktop.h
    session.cpp
    session.h
    session_p.h)

if(MARUPOP_PLATFORM_WAYLAND)
    target_sources(marupop_lib PRIVATE
        backend_wayland.cpp
        desktop_kde.cpp
        session_wayland.cpp)
    # Qt6::DBus carries the org.kde.KWin name probe in session_wayland.cpp. src/CMakeLists.txt
    # already links it PUBLIC; it is named here as well so the dependency is recorded where the
    # source that needs it is.
    target_link_libraries(marupop_lib PUBLIC Qt6::DBus)
else()
    target_sources(marupop_lib PRIVATE
        backend_win.cpp
        desktop_win.cpp
        session_win.cpp
        singleinstance.cpp
        singleinstance.h)
    target_link_libraries(marupop_lib PRIVATE shell32 ole32)
endif()
