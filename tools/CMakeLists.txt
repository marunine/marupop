# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# Development tools: standalone feasibility probes, plus install-dev-desktop.sh, which is a
# script rather than a target. README.md beside this file says what each one does and why it
# is kept.
#
# The option is the stated policy: a probe target exists in every configure, so it can be built
# by name at any time, and MARUPOP_BUILD_DEV_TOOLS=ON adds every one of them to the default
# target for a build that has to prove they still compile. None of them is ever installed.
#
# Each probe is add_executable(marupop-<name> ${dev_tool_scope} <name>.cpp) linking
# marupop_lib. The ONNX Runtime execution-provider report arrives with ocr/.
option(MARUPOP_BUILD_DEV_TOOLS "Build the tools/ probes as part of the default target" OFF)
if(MARUPOP_BUILD_DEV_TOOLS)
    set(dev_tool_scope "")
else()
    set(dev_tool_scope EXCLUDE_FROM_ALL)
endif()
add_feature_info(DevTools MARUPOP_BUILD_DEV_TOOLS "the tools/ probes in the default target")

# The popup probe: shows one sample card on the live session next to the pointer. The
# layer-shell surface, the margins against the target screen's origin and the pitch-accent
# overlay are only observable on a Wayland session, which no test platform provides.
add_executable(marupop-popupprobe ${dev_tool_scope} popupprobe.cpp)
target_link_libraries(marupop-popupprobe PRIVATE marupop_lib)

# Capture and cursor probes report latency through the production backends.
foreach(probe captureprobe cursorprobe)
    add_executable(marupop-${probe} ${dev_tool_scope} ${probe}.cpp)
    target_link_libraries(marupop-${probe} PRIVATE marupop_lib)
endforeach()

# Check authorization for an explicit executable or the configured installation path.
add_executable(marupop-authcheck ${dev_tool_scope} authcheck.cpp)
target_compile_definitions(marupop-authcheck
                           PRIVATE MARUPOP_INSTALLED_BINARY="${KDE_INSTALL_FULL_BINDIR}/marupop")
target_link_libraries(marupop-authcheck PRIVATE marupop_lib)

# The dictionary manager probe: opens the Manage Dictionaries dialog against a scratch dictionary
# directory. The drag reordering, the icon-theme decorations and the message widget's action
# buttons are only observable on a real session, which the offscreen test platform is not.
add_executable(marupop-dictuiprobe ${dev_tool_scope} dictuiprobe.cpp)
target_link_libraries(marupop-dictuiprobe PRIVATE marupop_lib)

# importprobe drives maru::dict::DictionaryManager from a terminal: it points a built-in entry at
# a local dump, adds a Yomitan folder, and runs the import job to completion with the wall time
# and the resulting file sizes. That is what fills a real installation without the manager dialog,
# and provides repeatable import timings.
add_executable(marupop-importprobe ${dev_tool_scope} importprobe.cpp)
target_link_libraries(marupop-importprobe PRIVATE marupop_lib)

# lookupprobe times maru::lookup::Engine over a dictionary directory the user actually has. The
# engine's cost scales with the number of enabled dictionaries and with the span under the
# pointer, and a hermetic fixture holds one small dictionary, so the hover latency of a real
# configuration is only observable from a driver pointed at a real one.
add_executable(marupop-lookupprobe ${dev_tool_scope} lookupprobe.cpp)
target_link_libraries(marupop-lookupprobe PRIVATE marupop_lib)


# hoverprobe paints Japanese text at a known logical rectangle (a layer surface, because a plain
# toplevel cannot place itself) and warps the pointer onto a named character, which is the only
# way to drive the whole pointer -> grab -> OCR -> lookup -> popup chain from a script. The warp
# goes through org_kde_kwin_fake_input, so the probe generates that protocol's client stubs for
# itself: nothing else in the tree binds a raw Wayland global, and marupop_lib must not grow a
# dependency on one.
find_package(PlasmaWaylandProtocols QUIET)
find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner)
pkg_check_modules(WaylandClient IMPORTED_TARGET wayland-client)
if(PlasmaWaylandProtocols_FOUND AND WAYLAND_SCANNER_EXECUTABLE AND WaylandClient_FOUND)
    set(fake_input_xml "${PLASMA_WAYLAND_PROTOCOLS_DIR}/fake-input.xml")
    set(fake_input_header "${CMAKE_CURRENT_BINARY_DIR}/wayland-fake-input-client-protocol.h")
    set(fake_input_source "${CMAKE_CURRENT_BINARY_DIR}/wayland-fake-input-protocol.c")
    add_custom_command(OUTPUT "${fake_input_header}"
                       COMMAND ${WAYLAND_SCANNER_EXECUTABLE} client-header "${fake_input_xml}" "${fake_input_header}"
                       DEPENDS "${fake_input_xml}"
                       VERBATIM)
    add_custom_command(OUTPUT "${fake_input_source}"
                       COMMAND ${WAYLAND_SCANNER_EXECUTABLE} private-code "${fake_input_xml}" "${fake_input_source}"
                       DEPENDS "${fake_input_xml}"
                       VERBATIM)
    add_executable(marupop-hoverprobe ${dev_tool_scope} hoverprobe.cpp "${fake_input_header}" "${fake_input_source}")
    target_include_directories(marupop-hoverprobe PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(marupop-hoverprobe PRIVATE marupop_lib PkgConfig::WaylandClient)
else()
    message(STATUS "marupop-hoverprobe: no plasma-wayland-protocols, wayland-scanner or wayland-client; skipped")
endif()
