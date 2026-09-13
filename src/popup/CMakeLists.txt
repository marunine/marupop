# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# The popup card: the theme, the rich-text renderer, the pitch-accent overlay, the placement
# math and the layer-shell window.
#
# No target_link_libraries() entry is needed here. Qt6::Widgets and LayerShellQt::Interface are
# already PUBLIC on marupop_lib (src/CMakeLists.txt), which is what LayerShellQt/Window in
# popupwindow.cpp and QTextBrowser in popupview.h resolve through.
target_sources(marupop_lib PRIVATE
    entrymodel.h
    pitchpainter.cpp
    pitchpainter.h
    placement.cpp
    placement.h
    popuppreview.cpp
    popuppreview.h
    popupview.cpp
    popupview.h
    popupwindow.cpp
    popupwindow.h
    renderer.cpp
    renderer.h
    renderoptions.cpp
    renderoptions.h
    theme.cpp
    theme.h)
