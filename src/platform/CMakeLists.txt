# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# Session detection and the factory that builds the four session-dependent services.
# Everything here lives in the maru::platform namespace.
#
# This module is listed after capture/, cursor/, popup/ and app/ in src/CMakeLists.txt, because
# backend.cpp names every implementation of the four interfaces and so includes a header from
# each of those modules. The dependency runs one way: nothing in those modules includes a
# platform/ header except app/application.cpp.
target_sources(marupop_lib PRIVATE
    backend.cpp
    backend.h
    session.cpp
    session.h)

# Qt6::DBus carries the org.kde.KWin name probe in session.cpp. src/CMakeLists.txt already links
# it PUBLIC; it is named here as well so the dependency is recorded where the source that needs
# it is.
target_link_libraries(marupop_lib PUBLIC Qt6::DBus)
