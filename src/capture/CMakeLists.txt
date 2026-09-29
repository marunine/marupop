# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# Screen-region capture. Everything here lives in the maru::capture namespace, including the
# three files copied from marusnap, where they were in maru.
target_sources(marupop_lib PRIVATE
    framesource.cpp
    framesource.h
    scanregion.cpp
    scanregion.h
    screenlayout.cpp
    screenlayout.h)

if(MARUPOP_PLATFORM_WAYLAND)
    target_sources(marupop_lib PRIVATE
        authorization.cpp
        authorization.h
        hyprlandconfig.cpp
        hyprlandconfig.h
        kwinframesource.cpp
        kwinframesource.h
        kwingrabber.cpp
        kwingrabber.h
        wlrframesource.cpp
        wlrframesource.h)
    # KF6::Service supplies KApplicationTrader and KService, which authorization.cpp repeats
    # KWin's desktop-entry lookup with. src/CMakeLists.txt already links it PRIVATE for the same
    # file; it is named here as well so the dependency is recorded where the source that needs it
    # is.
    target_link_libraries(marupop_lib PRIVATE KF6::Service)
else()
    target_sources(marupop_lib PRIVATE
        winframesource.cpp
        winframesource.h)
    # gdi32, which exports BitBlt, is in CMake's default link libraries for a Windows target.
    target_link_libraries(marupop_lib PRIVATE d3d11 dxgi)
endif()

# XXH3-64 hashes captured rows to avoid repeated OCR on unchanged regions. Its headers do not
# appear in the capture API, so the link remains private.
target_link_libraries(marupop_lib PRIVATE marupop::xxhash)
