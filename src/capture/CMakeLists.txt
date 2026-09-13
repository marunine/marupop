# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# Screen-region capture. Everything here lives in the maru::capture namespace, including the
# three files copied from marusnap, where they were in maru.
target_sources(marupop_lib PRIVATE
    authorization.cpp
    authorization.h
    framesource.cpp
    framesource.h
    hyprlandconfig.cpp
    hyprlandconfig.h
    kwingrabber.cpp
    kwingrabber.h
    scanregion.cpp
    scanregion.h
    screenlayout.cpp
    screenlayout.h
    wlrframesource.cpp
    wlrframesource.h)

# XXH3-64 hashes captured rows to avoid repeated OCR on unchanged regions. Its headers do not
# appear in the capture API, so the link remains private.
#
# KF6::Service supplies KApplicationTrader and KService, which authorization.cpp repeats KWin's
# desktop-entry lookup with. src/CMakeLists.txt already links it PRIVATE for the same file; it
# is named here as well so the dependency is recorded where the source that needs it is.
target_link_libraries(marupop_lib PRIVATE PkgConfig::XXHASH KF6::Service)
