# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# Everything except main() lives in a static library so tests can link it. core/ is listed
# here; every other module adds its own sources through target_sources(marupop_lib PRIVATE ...)
# in src/<module>/CMakeLists.txt, which is the file that module's owner edits.
add_library(marupop_lib STATIC
    core/enums.h
    core/layerscope.h
    core/logging.cpp
    core/logging.h
    core/paths.cpp
    core/paths.h
    core/settings.cpp
    core/settings.h)

kconfig_add_kcfg_files(marupop_lib GENERATE_MOC core/marupopsettings.kcfgc)

# wayland-scanner turns each vendored XML into wayland-<basename>-client-protocol.h and
# wayland-<basename>-protocol.c, which src/wayland/, src/capture/, src/cursor/ and src/app/
# include. The XML files are copies rather than a build-time dependency on wayland-protocols and
# hyprland-protocols; third_party/protocols/README.md records the trees and the commits they were
# copied from.
#
# Called here rather than from src/wayland/CMakeLists.txt: ecm_add_wayland_client_protocol()
# expands to an add_custom_command(), whose OUTPUT can only be consumed by a target defined in
# the same directory as the command, and marupop_lib is defined here. The generated headers
# therefore land in this directory's binary dir, which is already on marupop_lib's PUBLIC
# include path.
#
# PRIVATE_CODE hides the generated marshalling symbols. marupop_lib is a static library linked
# into several executables, and an exported wl_interface symbol would collide with the same
# symbol from another library that generated the same protocol.
if(MARUPOP_PLATFORM_WAYLAND)
    set(marupop_wayland_protocol_sources "")
    foreach(protocol IN ITEMS
            wlr-screencopy-unstable-v1
            hyprland-lock-notify-v1
            hyprland-global-shortcuts-v1)
        ecm_add_wayland_client_protocol(marupop_wayland_protocol_sources
            PROTOCOL ${CMAKE_SOURCE_DIR}/third_party/protocols/${protocol}.xml
            BASENAME ${protocol}
            PRIVATE_CODE)
    endforeach()
    target_sources(marupop_lib PRIVATE ${marupop_wayland_protocol_sources})
endif()

# The deconjugation rule set and the KWin cursor relay package. The rule set is read by
# deconj::RuleSet::loadEmbedded(); the relay package is written to
# ~/.local/share/kwin/scripts/marupopcursor/ by cursor/kwinscriptrelay.cpp when the installed
# copy under ${KDE_INSTALL_DATADIR}/marupop/kwin-script/ is absent, which is the case for a
# build-tree run.
#
# The generated translation unit goes into an object library rather than into marupop_lib
# itself. A static library contributes a member only where something already references a
# symbol in it, and nothing references qInitResources_marupop(): the registration runs from a
# namespace-scope initializer that no caller names. Putting the object on marupop_lib's link
# interface is what makes every executable linking marupop_lib carry it, and leaving it a
# member of the archive would instead leave each of them with an empty :/marupop prefix and no
# diagnostic.
set(marupop_qrc_files ${CMAKE_SOURCE_DIR}/data/marupop.qrc)
if(MARUPOP_PLATFORM_WIN32)
    list(APPEND marupop_qrc_files ${CMAKE_SOURCE_DIR}/icons/marupop-icons.qrc)
endif()
qt_add_resources(marupop_resource_sources ${marupop_qrc_files})
add_library(marupop_resources OBJECT ${marupop_resource_sources})
target_link_libraries(marupop_resources PRIVATE Qt6::Core)
# INTERFACE_SOURCES rather than a target_link_libraries() entry: CMake adds an object library's
# object files to the target that links it and stops there, so the objects reach neither the
# executable nor a test binary through marupop_lib. Qt's own
# __qt_internal_propagate_object_files() propagates a static library's resources the same way.
target_sources(marupop_lib INTERFACE $<TARGET_OBJECTS:marupop_resources>)

# kdebugsettings lists a category only when an installed .categories file declares it, and
# ecm_qt_install_logging_categories() below generates that file from the registrations here.
#
# The categories are declared by hand in core/logging.h and defined in core/logging.cpp, which
# keeps every one of them in one place and lets logCategoryNames() read the names off the
# category objects. ecm_qt_export_logging_category() is the registration for exactly that case;
# ecm_qt_declare_logging_category() would instead generate a header and a source per category
# and replace the single file with two generated ones per category.
#
# The DESCRIPTION strings are the text kdebugsettings shows beside each checkbox. logging_test
# reads the generated file and fails on a category the code defines that is missing a
# registration here, and on a registration whose category the code no longer defines.
macro(marupop_logging_category suffix identifier description)
    ecm_qt_export_logging_category(
        IDENTIFIER "maru::${identifier}"
        CATEGORY_NAME "marupop.${suffix}"
        DESCRIPTION "MaruPop ${description}"
        EXPORT MaruPop)
endmacro()

marupop_logging_category(app logApp "application shell")
marupop_logging_category(capture logCapture "screen region capture")
marupop_logging_category(core logCore "settings and paths")
marupop_logging_category(cursor logCursor "pointer tracking")
marupop_logging_category(deconj logDeconj "deconjugation")
marupop_logging_category(dict logDict "dictionary stores")
marupop_logging_category(dictimport logDictImport "dictionary import")
marupop_logging_category(jp logJp "Japanese text handling")
marupop_logging_category(kwingrabber logKWinGrabber "KWin ScreenShot2 grabber")
marupop_logging_category(lookup logLookup "dictionary lookup")
marupop_logging_category(meikiocr logMeikiOcr "meikiocr backend")
marupop_logging_category(ocr logOcr "text recognition")
marupop_logging_category(platform logPlatform "session detection and backend choice")
marupop_logging_category(popup logPopup "result popup")
marupop_logging_category(scan logScan "scan controller")
marupop_logging_category(screenai logScreenAi "Screen AI backend")
marupop_logging_category(screenaiproto logScreenAiProto "Screen AI protobuf codec")
marupop_logging_category(wayland logWayland "Wayland protocol clients")
marupop_logging_category(win32 logWin32 "Win32 windows, hotkeys and session notifications")

ecm_qt_install_logging_categories(
    EXPORT MaruPop
    FILE marupop.categories
    DESTINATION ${KDE_INSTALL_LOGGINGCATEGORIESDIR}
    SORT)

# The binary dir carries the kconfig-generated marupopsettings.h and the ecm_setup_version()
# generated marupop_version.h. PUBLIC is what puts both on main.cpp's include path.
target_include_directories(marupop_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

# The reverse-DNS application id, from the one variable the desktop entry, the notifyrc, the
# D-Bus service name and the icon names also come from. PUBLIC, because main.cpp reads it too.
target_compile_definitions(marupop_lib PUBLIC MARUPOP_APPLICATION_ID="${MARUPOP_APPLICATION_ID}")

# Qt, KF6 and the Wayland build's LayerShellQt are PUBLIC: their types appear in headers
# marupop_lib installs on its own include path, so a test binary or main.cpp that includes one
# of those headers needs the same include directories and the same link line.
#
# ONNX Runtime, OpenCV, sqlite3 and xxhash are deliberately absent here. Each is used by one
# module, behind headers that expose none of its types, so the owning src/<module>/CMakeLists.txt
# links it PRIVATE: ocr/ takes marupop::onnxruntime and ${OpenCV_LIBS}, dict/ takes
# marupop::sqlite3, and dict/ and capture/ take marupop::xxhash. Adding one of them here would put
# its headers on every translation unit's include path and its symbols on every test binary's
# link line.
target_link_libraries(marupop_lib
    PUBLIC
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::Network
        Qt6::Sql
        KF6::Archive
        KF6::ColorScheme
        KF6::ConfigCore
        KF6::ConfigGui
        KF6::ConfigWidgets
        KF6::CoreAddons
        KF6::GuiAddons
        KF6::I18n
        KF6::WidgetsAddons
    PRIVATE
        ${CMAKE_DL_LIBS})
if(MARUPOP_PLATFORM_WAYLAND)
    target_link_libraries(marupop_lib
        PUBLIC
            Qt6::DBus
            KF6::GlobalAccel
            KF6::JobWidgets
            KF6::KIOCore
            KF6::KIOGui
            KF6::KIOWidgets
            KF6::Notifications
            KF6::StatusNotifierItem
            KF6::XmlGui
            LayerShellQt::Interface
        PRIVATE
            # KApplicationTrader and KService, for the desktop-entry lookup
            # capture/authorization.cpp repeats after KWin. No header exposes either type, so the
            # link stays private.
            KF6::Service)
else()
    add_dependencies(marupop_lib marupop_onnxruntime_dll)
    target_link_libraries(marupop_lib PRIVATE Qt6::Svg)
    # _WIN32_WINNT and WINVER at 0x0A00 select the Windows 10 declarations of the SDK headers.
    # SetWindowDisplayAffinity accepts WDA_EXCLUDEFROMCAPTURE from Windows 10 version 2004
    # (build 19041).
    target_compile_definitions(marupop_lib PUBLIC _WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()

add_subdirectory(jp)
add_subdirectory(deconj)
add_subdirectory(dict)
add_subdirectory(lookup)
add_subdirectory(ocr)
# wayland/ before capture/ and cursor/: it generates the protocol headers those two include, and
# an ecm_add_wayland_client_protocol() target has to exist before a source that includes its
# output is compiled.
if(MARUPOP_PLATFORM_WAYLAND)
    add_subdirectory(wayland)
else()
    add_subdirectory(win32)
endif()
add_subdirectory(capture)
add_subdirectory(cursor)
add_subdirectory(scan)
add_subdirectory(popup)
add_subdirectory(dictui)
# platform/ last of the library modules: backend.cpp names every implementation of the four
# session-dependent interfaces, so it includes a header from capture/, cursor/ and app/.
add_subdirectory(platform)
add_subdirectory(app)

add_executable(marupop main.cpp)
target_link_libraries(marupop PRIVATE marupop_lib)
if(MARUPOP_PLATFORM_WAYLAND)
    target_link_libraries(marupop PRIVATE KF6::Crash KF6::DBusAddons)
else()
    # Windows starts a GUI-subsystem executable without a console window, from the Run key or a
    # shortcut.
    set_target_properties(marupop PROPERTIES WIN32_EXECUTABLE ON)
    # vcpkg's applocal step copies DLLs beside each executable and leaves the Qt plugins in the
    # vcpkg installed tree.
    set(qt_plugin_dir "$<PATH:GET_PARENT_PATH,$<PATH:GET_PARENT_PATH,$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>>>")
    file(GENERATE
         OUTPUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/qt.conf
         CONTENT "[Paths]\nPlugins = ${qt_plugin_dir}\n")
    # The runtime output folder also holds the DLLs that only the test binaries link.
    install(DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/
            DESTINATION ${KDE_INSTALL_BINDIR}
            FILES_MATCHING PATTERN "*.dll"
            PATTERN "gtest*.dll" EXCLUDE
            PATTERN "gmock*.dll" EXCLUDE
            PATTERN "Qt6Test*.dll" EXCLUDE)
    foreach(plugins IN ITEMS platforms styles imageformats iconengines sqldrivers tls networkinformation)
        install(DIRECTORY "${qt_plugin_dir}/${plugins}"
                DESTINATION ${KDE_INSTALL_BINDIR}/plugins
                FILES_MATCHING PATTERN "*.dll"
                PATTERN "qoffscreen*" EXCLUDE
                PATTERN "qminimal*" EXCLUDE
                PATTERN "qdirect2d*" EXCLUDE)
    endforeach()
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/qt-install.conf "[Paths]\nPlugins = plugins\n")
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/qt-install.conf DESTINATION ${KDE_INSTALL_BINDIR} RENAME qt.conf)
    # Task Manager's Startup page and the Windows notification settings show the FileDescription
    # of the version resource as the application name.
    configure_file(marupop.rc.cmake ${CMAKE_CURRENT_BINARY_DIR}/marupop.rc @ONLY)
    target_sources(marupop PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/marupop.rc)
endif()

install(TARGETS marupop ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
