# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
cmake_minimum_required(VERSION 3.25)
project(marupop VERSION 0.1.0 LANGUAGES C CXX)

# QWaylandScreen requires Qt 6.7; KKeySequenceRecorder::setPatterns requires KF 6.12.
set(QT_MIN_VERSION 6.7)
set(KF_MIN_VERSION 6.12)
# 1.20 is where wl_proxy_marshal_flags landed, which wayland-scanner emits for every request in
# the generated protocol sources.
set(WAYLAND_MIN_VERSION 1.20)

# The reverse-DNS application id, in the io.github. form Flathub verifies a GitHub-hosted
# project against. Everything that has to carry it derives from this one variable: the desktop
# entry's file name and Icon=, the notifyrc DesktopEntry and IconName, the AppStream <id>, the
# D-Bus service name the KWin cursor relay calls, and the MARUPOP_APPLICATION_ID the sources
# read for setDesktopFileName(), the icon lookups and the autostart entry. The icon and
# metainfo *file* names spell it out, since those are installed under it. org.kde.* is KDE's
# namespace and is not available to this project.
set(MARUPOP_APPLICATION_ID "io.github.marunine.marupop")

# Ahead of KDECompilerSettings, which defaults an unset build type to Debug for a source
# directory under version control. Every default build of the OCR, deconjugation and lookup
# paths would otherwise carry no optimization flag at all. Written to the cache, so cmake-gui
# and a reader of CMakeCache.txt see the type the compile lines are built from.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# extra-cmake-modules carries the KDE and XDG install-path conventions, the compiler and Qt
# settings every KDE application builds with, and the version-header and logging-category
# generators used below. Hand-rolling those produces a package each distributor has to patch.
find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(FeatureSummary)
include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMInstallIcons)
include(ECMQtDeclareLoggingCategory)
include(ECMSetupVersion)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# main.cpp reads MARUPOP_VERSION_STRING from this header, so the version the About dialog shows
# is the one project() declares. Written into the binary directory that marupop_lib already
# puts on its include path.
ecm_setup_version(PROJECT
                  VARIABLE_PREFIX MARUPOP
                  VERSION_HEADER "${CMAKE_BINARY_DIR}/src/marupop_version.h")

# Widgets for the whole UI, DBus for the KWin cursor relay and the ScreenShot2 grabber,
# Concurrent for the OCR and import worker pools, Network for the model and dictionary
# downloads, Sql for the dictionary import and administration paths (the read path uses the
# sqlite3 C API directly).
find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Widgets DBus Concurrent Network Sql)
find_package(KF6Config ${KF_MIN_VERSION} REQUIRED)        # KConfigXT settings
find_package(KF6ConfigWidgets ${KF_MIN_VERSION} REQUIRED) # KConfigDialog settings window, KStyleManager
find_package(KF6CoreAddons ${KF_MIN_VERSION} REQUIRED)    # KAboutData, KJob
find_package(KF6Crash ${KF_MIN_VERSION} REQUIRED)         # DrKonqi handler for a tray-resident process
find_package(KF6XmlGui ${KF_MIN_VERSION} REQUIRED)        # KAboutApplicationDialog only; no KXmlGuiWindow
find_package(KF6I18n ${KF_MIN_VERSION} REQUIRED)
find_package(KF6DBusAddons ${KF_MIN_VERSION} REQUIRED)    # KDBusService(Unique)
find_package(KF6GlobalAccel ${KF_MIN_VERSION} REQUIRED)   # in-process global shortcuts
find_package(KF6GuiAddons ${KF_MIN_VERSION} REQUIRED)     # KSystemClipboard, KKeySequenceRecorder
find_package(KF6Service ${KF_MIN_VERSION} REQUIRED)       # KWin's desktop-entry lookup, in capture/authorization
find_package(KF6KIO ${KF_MIN_VERSION} REQUIRED)           # download jobs, highlightInFileManager
find_package(KF6Notifications ${KF_MIN_VERSION} REQUIRED) # marupop.notifyrc events
find_package(KF6StatusNotifierItem ${KF_MIN_VERSION} REQUIRED) # tray icon
find_package(KF6WidgetsAddons ${KF_MIN_VERSION} REQUIRED) # KMessageWidget in the dictionary manager
find_package(KF6Archive ${KF_MIN_VERSION} REQUIRED)       # KZip, for Yomitan dictionary archives
find_package(KF6JobWidgets ${KF_MIN_VERSION} REQUIRED)    # KJob progress in the dictionary manager
find_package(KF6ColorScheme ${KF_MIN_VERSION} REQUIRED)   # popup theme presets against the session palette
# The popup is positioned by the compositor: a plain Wayland toplevel cannot place itself at an
# arbitrary global coordinate, and the popup has to sit beside the pointer.
find_package(LayerShellQt REQUIRED)
# The three protocols a wlroots-family compositor answers where KDE Plasma answers over D-Bus:
# zwlr_screencopy_unstable_v1 for region pixels, hyprland_lock_notify_v1 for the session lock
# state and hyprland_global_shortcuts_v1 for the three hotkeys. Qt's platform plugin binds none
# of them, so src/wayland/ binds them itself on the wl_display the plugin already owns.
#
# The two ECM find modules supply the halves that need finding: Wayland::Client for the library
# and WaylandScanner for ecm_add_wayland_client_protocol(). The protocol XML is vendored under
# third_party/protocols/, so no wayland-protocols or hyprland-protocols package is required.
find_package(Wayland ${WAYLAND_MIN_VERSION} REQUIRED COMPONENTS Client)
find_package(WaylandScanner REQUIRED)
find_package(PkgConfig REQUIRED)
# ONNX Runtime executes the meikiocr detection and recognition models. Arch ships the
# libonnxruntime.pc file under that name rather than onnxruntime.
pkg_check_modules(ONNXRUNTIME REQUIRED IMPORTED_TARGET libonnxruntime)
# The dictionary read path drives sqlite3 through its C API, for PRAGMA query_only, a set
# mmap_size and one cached prepared statement per store.
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
# xxh3, for the frame hash that skips an OCR pass over an unchanged frame and for the sorted
# key-filter sidecar that rejects a dictionary miss before the sqlite3 query.
pkg_check_modules(XXHASH REQUIRED IMPORTED_TARGET libxxhash)
# The meikiocr preprocessing steps are one bilinear resize and one color conversion, which is
# what these two components supply. The full OpenCV package is not required.
find_package(OpenCV 5 REQUIRED COMPONENTS core imgproc)

# The target and optional hook both use the checked-in formatting policy. ECM also preserves
# an existing custom .clang-format; the explicit target keeps its source scope visible here.
find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
add_feature_info(ClangFormat CLANG_FORMAT_EXECUTABLE "the clang-format target")
if(CLANG_FORMAT_EXECUTABLE)
    # Every directory holding a tracked .cpp or .h, because the other two consumers work over
    # `git ls-files '*.cpp' '*.h'` and a target covering less than they check leaves a file the
    # hook rejects and this target does not rewrite.
    file(GLOB_RECURSE MARUPOP_FORMAT_SOURCES CONFIGURE_DEPENDS
         ${CMAKE_SOURCE_DIR}/src/*.cpp ${CMAKE_SOURCE_DIR}/src/*.h
         ${CMAKE_SOURCE_DIR}/tests/*.cpp ${CMAKE_SOURCE_DIR}/tests/*.h
         ${CMAKE_SOURCE_DIR}/tools/*.cpp ${CMAKE_SOURCE_DIR}/tools/*.h)
    # Rewrites the tree in place, through the same .clang-format the hook and the CI format job
    # read, so all three agree on what the output is.
    add_custom_target(clang-format
                      COMMAND ${CLANG_FORMAT_EXECUTABLE} -i ${MARUPOP_FORMAT_SOURCES}
                      COMMENT "Formatting ${CMAKE_SOURCE_DIR} through clang-format"
                      VERBATIM)
endif()

# core.hooksPath is per-checkout configuration, so a clone runs no hook until something sets it.
# ECM's kde_configure_git_pre_commit_hook() writes .git/hooks/pre-commit for the same reason;
# pointing at the tracked directory instead keeps the hook itself under version control.
option(MARUPOP_GIT_HOOKS "Point this checkout's core.hooksPath at .githooks" OFF)
if(MARUPOP_GIT_HOOKS AND EXISTS ${CMAKE_SOURCE_DIR}/.git)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND ${GIT_EXECUTABLE} config --local --get core.hooksPath
                        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                        OUTPUT_VARIABLE configured_hooks_path
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET)
        # core.hooksPath replaces the whole hooks directory rather than one hook, so a checkout
        # that already runs hooks would lose every one of them: a Gerrit commit-msg hook, a
        # pre-commit-framework install, anything else written into .git/hooks/. Both questions are
        # asked. A non-empty core.hooksPath is a path the developer chose; a .git/hooks/ holding
        # anything but git's own .sample files is a set of hooks the developer installed. Either
        # answer leaves the checkout as it is, and `git config core.hooksPath .githooks` is what
        # opts in afterwards.
        execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --git-path hooks
                        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                        OUTPUT_VARIABLE git_hooks_dir
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET)
        set(installed_hooks "")
        if(git_hooks_dir AND IS_DIRECTORY ${CMAKE_SOURCE_DIR}/${git_hooks_dir})
            file(GLOB installed_hooks ${CMAKE_SOURCE_DIR}/${git_hooks_dir}/*)
            list(FILTER installed_hooks EXCLUDE REGEX "\\.sample$")
        endif()
        if(configured_hooks_path STREQUAL "" AND installed_hooks STREQUAL "")
            execute_process(COMMAND ${GIT_EXECUTABLE} config --local core.hooksPath .githooks
                            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
            message(STATUS "core.hooksPath set to .githooks for the clang-format pre-commit hook")
        endif()
    endif()
endif()

# Compiles every po/<lang>/marupop.po into a catalogue under ${KDE_INSTALL_LOCALEDIR}, which is
# where KLocalizedString::setApplicationDomain("marupop") in main.cpp looks. po/ holds the
# template alone until a translation arrives, and ki18n_install() installs nothing for a
# language directory that is not there.
#
# po/marupop.pot is produced by po/extract-messages.sh and committed, rather than by KDE's
# scripty robots, which service invent.kde.org and no other host. A translation reaches this
# project as a pull request adding po/<lang>/marupop.po.
ki18n_install(po)

add_subdirectory(src)
add_subdirectory(data)
add_subdirectory(icons)
add_subdirectory(tools)

# Preserve the notice's relative license paths for binary recipients.
set(MARUPOP_NOTICE_DIR ${KDE_INSTALL_DATADIR}/doc/marupop)
install(FILES LICENSE.TXT NOTICE DESTINATION ${MARUPOP_NOTICE_DIR})
install(DIRECTORY LICENSES/ DESTINATION ${MARUPOP_NOTICE_DIR}/LICENSES)
foreach(component IN ITEMS JL nazeka similar-kanji)
    install(FILES third_party/${component}/LICENSE
            DESTINATION ${MARUPOP_NOTICE_DIR}/third_party/${component})
endforeach()
install(DIRECTORY third_party/protocols/
        DESTINATION ${MARUPOP_NOTICE_DIR}/third_party/protocols
        FILES_MATCHING PATTERN "*.xml" PATTERN "README.md")

if(BUILD_TESTING)
    find_package(GTest REQUIRED)
    find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
