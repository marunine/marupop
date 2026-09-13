# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# Pointer tracking. Everything here lives in the maru::cursor namespace.
target_sources(marupop_lib PRIVATE
    cursorsink.cpp
    cursorsink.h
    cursortracker.cpp
    cursortracker.h
    hyprcursortracker.cpp
    hyprcursortracker.h
    hyprsocket.cpp
    hyprsocket.h
    kwinscriptrelay.cpp
    kwinscriptrelay.h
    lockwatcher.cpp
    lockwatcher.h
    screensaverlockwatcher.cpp
    screensaverlockwatcher.h
    wlrlockwatcher.cpp
    wlrlockwatcher.h)

# Qt6::DBus carries the sink, the org.kde.kwin.Scripting bootstrap and the screen saver watcher;
# KF6::ConfigCore carries the kwinrc edit that enables the relay package. src/CMakeLists.txt
# already links both PUBLIC, because Qt6::DBus types appear in cursor/cursorsink.h and
# KSharedConfig::Ptr in cursor/kwinscriptrelay.h. They are named here as well so the dependency
# is recorded where the sources that need it are.
#
# Qt6::Network carries the QLocalSocket the Hyprland pointer poll drives; src/CMakeLists.txt
# already links it PUBLIC for the model and dictionary downloads.
target_link_libraries(marupop_lib PUBLIC Qt6::DBus Qt6::Network KF6::ConfigCore)
