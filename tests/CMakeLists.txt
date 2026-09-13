# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# Unit tests: GoogleTest for pure logic, Qt Test for Qt-facing components.

# Each test binary gets its own HOME. Test mode redirects to $HOME/.qttest and ignores
# XDG_CONFIG_HOME, so a shared HOME would put every binary on one marupoprc: settingsdialog_test
# and settings_test assert on the same keys, and under `ctest -j` they would race for them. A
# snapshot taken inside one process cannot protect the other, which is why the isolation belongs
# here (marusnap/tests/CMakeLists.txt is where this came from).
#
# testenvironment.cpp, linked into every target below, sets the same HOME and turns
# QStandardPaths test mode on, both before main() runs. HOME is set twice on purpose: ctest's
# ENVIRONMENT property covers a `ctest` run, and the linked file covers a binary started
# straight from the build directory, which is how a --gtest_filter pass over one failing case
# is run. Test mode is set there alone, because a suite taking main() from GTest::gtest_main or
# QTEST_MAIN has no place to call it.
function(maru_test_environment name)
    set(home ${CMAKE_CURRENT_BINARY_DIR}/home/${name})
    file(MAKE_DIRECTORY ${home})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${home}")
    target_sources(${name} PRIVATE ${CMAKE_SOURCE_DIR}/tests/testenvironment.cpp)
    target_compile_definitions(${name} PRIVATE MARUPOP_TEST_HOME="${home}")
endfunction()

# maru_add_gtest(<name> <sources...>) — GTest-based, links marupop_lib.
function(maru_add_gtest name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE marupop_lib GTest::gtest GTest::gtest_main Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    maru_test_environment(${name})
endfunction()

# Variant for suites that provide their own main() (a Q*Application plus an event loop).
function(maru_add_gtest_app name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE marupop_lib GTest::gtest Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    maru_test_environment(${name})
endfunction()

# maru_add_qttest(<name> <sources...>) — Qt Test-based, links marupop_lib.
# Available for future Qt Test suites alongside the GoogleTest helpers.
function(maru_add_qttest name)
    add_executable(${name} ${ARGN})
    set_target_properties(${name} PROPERTIES AUTOMOC ON)
    target_link_libraries(${name} PRIVATE marupop_lib Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    maru_test_environment(${name})
endfunction()

# A suite that needs a private session bus, which is how a fake service registers a well-known
# name the production code looks up: tests/support/privatebus.h refuses to register on any other
# bus. dbus-run-session starts a dbus-daemon, exports DBUS_SESSION_BUS_ADDRESS and runs the
# command, so the bus lives exactly as long as the test binary.
find_program(MARUPOP_DBUS_RUN_SESSION_EXECUTABLE dbus-run-session)

# maru_add_gtest_dbus(<name> <sources...>) — as maru_add_gtest_app, run on a private session bus.
# The test is registered whether or not dbus-run-session exists: without it the binary runs on
# whatever bus the environment supplies, MARUPOP_TEST_PRIVATE_BUS stays unset, and every case
# that needs a fake service self-skips.
function(maru_add_gtest_dbus name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE marupop_lib marupop_testsupport GTest::gtest Qt6::Test)
    if(MARUPOP_DBUS_RUN_SESSION_EXECUTABLE)
        add_test(NAME ${name} COMMAND ${MARUPOP_DBUS_RUN_SESSION_EXECUTABLE} -- $<TARGET_FILE:${name}>)
    else()
        add_test(NAME ${name} COMMAND ${name})
    endif()
    maru_test_environment(${name})
    if(MARUPOP_DBUS_RUN_SESSION_EXECUTABLE)
        # Appended rather than set, because maru_test_environment() owns QT_QPA_PLATFORM and HOME.
        set_property(TEST ${name} APPEND PROPERTY ENVIRONMENT "MARUPOP_TEST_PRIVATE_BUS=1")
    endif()
endfunction()

# A second registration of an existing test binary, run inside the nested KWin Wayland session
# that tests/harness/nested-session.sh starts. The binary is unchanged: the environment it finds
# there satisfies the MARUPOP_LIVE_* gates it already carries, so the live cases execute against
# a real compositor on a host with no logged-in Plasma session.
#
# maru_add_nested_test(<name> BINARY <target> [AUTHORIZE] [FAKE_INPUT] [LOCKSCREEN]
#                      [OUTPUTS <n>] [ENVIRONMENT <k=v>...] [ARGS <arg>...])
#
# AUTHORIZE writes the desktop entry that lets the binary call org.kde.KWin.ScreenShot2, and
# FAKE_INPUT the one that lets it bind org_kde_kwin_fake_input. OUTPUTS gives the session that
# many virtual outputs, which KWin lays out left to right; the default is the single output
# every other registration runs against.
function(maru_add_nested_test name)
    cmake_parse_arguments(ARG "AUTHORIZE;FAKE_INPUT;LOCKSCREEN" "BINARY;OUTPUTS" "ENVIRONMENT;ARGS" ${ARGN})
    set(harness ${CMAKE_SOURCE_DIR}/tests/harness/nested-session.sh)
    set(home ${CMAKE_CURRENT_BINARY_DIR}/nested-home/${name})
    file(MAKE_DIRECTORY ${home})

    set(options --home ${home})
    if(ARG_AUTHORIZE)
        list(APPEND options --authorize $<TARGET_FILE:${ARG_BINARY}>)
    endif()
    if(ARG_FAKE_INPUT)
        list(APPEND options --fake-input $<TARGET_FILE:${ARG_BINARY}>)
    endif()
    if(ARG_LOCKSCREEN)
        list(APPEND options --lockscreen)
    endif()
    # DEFINED rather than a truth test, which CMake answers false for the string "0": OUTPUTS 0
    # reaches the harness and fails there, rather than falling back to the default output count
    # with no diagnostic.
    if(DEFINED ARG_OUTPUTS)
        list(APPEND options --outputs ${ARG_OUTPUTS})
    endif()

    add_test(NAME ${name} COMMAND ${harness} ${options} -- $<TARGET_FILE:${ARG_BINARY}> ${ARG_ARGS})
    # 77 is the exit code nested-session.sh reports where kwin_wayland or dbus-run-session is
    # absent, which is every host that cannot run this form at all.
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77 LABELS nested TIMEOUT 300)
    # The binary keeps the MARUPOP_TEST_HOME compiled into it, so the nested registration and the
    # plain registration of the same target share one configuration directory. The lock keeps
    # `ctest -j` from running both at once against it.
    set_property(TEST ${name} APPEND PROPERTY RESOURCE_LOCK marupop-home-${ARG_BINARY})
    set_property(TEST ${ARG_BINARY} APPEND PROPERTY RESOURCE_LOCK marupop-home-${ARG_BINARY})
    foreach(entry IN LISTS ARG_ENVIRONMENT)
        set_property(TEST ${name} APPEND PROPERTY ENVIRONMENT "${entry}")
    endforeach()
endfunction()

# A second registration of an existing test binary, run inside the nested Hyprland session that
# tests/harness/hyprland-session.sh starts. The counterpart of maru_add_nested_test(), which does
# the same for kwin_wayland. The harness exports MARUPOP_LIVE_HYPRLAND=1, which is the gate the
# wlroots-family suites carry, so the live cases execute against a real Hyprland on a host with
# no Hyprland session logged in.
#
# maru_add_hyprland_test(<name> BINARY <target> [PERMISSION] [ENFORCE_ONLY] [OUTPUTS <n>]
#                        [ENVIRONMENT <k=v>...] [ARGS <arg>...])
#
# PERMISSION turns ecosystem:enforce_permissions on and writes the `permission` rules that grant
# the binary screencopy and cursorpos. ENFORCE_ONLY turns it on and writes no rule, which leaves
# every screencopy permission pending and is what covers the watchdog in
# capture::WlrFrameSource. The default leaves enforcement off, which is Hyprland's own default
# (Hyprland 0.56.0, src/config/values/ConfigValues.cpp). OUTPUTS sets the number of outputs,
# laid out left to right, to exercise the output-local coordinates of capture_output_region.
function(maru_add_hyprland_test name)
    cmake_parse_arguments(ARG "PERMISSION;ENFORCE_ONLY" "BINARY;OUTPUTS" "ENVIRONMENT;ARGS" ${ARGN})
    set(harness ${CMAKE_SOURCE_DIR}/tests/harness/hyprland-session.sh)
    set(home ${CMAKE_CURRENT_BINARY_DIR}/hyprland-home/${name})
    file(MAKE_DIRECTORY ${home})

    set(options --home ${home})
    if(ARG_PERMISSION)
        list(APPEND options --permission)
    endif()
    if(ARG_ENFORCE_ONLY)
        list(APPEND options --enforce-only)
    endif()
    if(DEFINED ARG_OUTPUTS)
        list(APPEND options --outputs ${ARG_OUTPUTS})
    endif()

    add_test(NAME ${name} COMMAND ${harness} ${options} -- $<TARGET_FILE:${ARG_BINARY}> ${ARG_ARGS})
    # 77 is the exit code hyprland-session.sh reports where Hyprland is absent or where it cannot
    # start a headless session, which is every host that cannot run this form at all.
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77 LABELS hyprland TIMEOUT 300)
    # The binary keeps the MARUPOP_TEST_HOME compiled into it, so this registration and the plain
    # registration of the same target share one configuration directory.
    set_property(TEST ${name} APPEND PROPERTY RESOURCE_LOCK marupop-home-${ARG_BINARY})
    set_property(TEST ${ARG_BINARY} APPEND PROPERTY RESOURCE_LOCK marupop-home-${ARG_BINARY})
    # Serialize nested Hyprland sessions even when their binaries and HOME directories differ.
    # The HOME lock alone cannot protect the compositor resources shared across registrations.
    # RUN_SERIAL would also exclude unrelated unit tests, so use a scoped resource lock instead.
    # If a loaded full run exceeds the screencopy timeout, rerun the Hyprland label in isolation.
    set_property(TEST ${name} APPEND PROPERTY RESOURCE_LOCK marupop-hyprland-session)
    foreach(entry IN LISTS ARG_ENVIRONMENT)
        set_property(TEST ${name} APPEND PROPERTY ENVIRONMENT "${entry}")
    endforeach()
endfunction()

add_subdirectory(support)
add_subdirectory(core)
add_subdirectory(jp)
add_subdirectory(deconj)
add_subdirectory(dict)
add_subdirectory(lookup)
add_subdirectory(ocr)
add_subdirectory(wayland)
add_subdirectory(capture)
add_subdirectory(cursor)
add_subdirectory(scan)
add_subdirectory(popup)
add_subdirectory(platform)
add_subdirectory(dictui)
add_subdirectory(app)
add_subdirectory(data)
add_subdirectory(nested)
