# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# The doubles and fake services the suites share. Linked by maru_add_gtest_dbus() targets, and
# by any other suite that adds it to target_link_libraries().
#
# A static library rather than a set of headers, because the fake D-Bus services carry Q_OBJECT
# and need AUTOMOC, and because one translation unit per fake keeps the moc output out of every
# suite that includes only fakes.h.
add_library(marupop_testsupport STATIC
    cropframesource.cpp
    privatebus.cpp
    fakehyprsocket.cpp
    fakekwin.cpp
    fakescreensaver.cpp
    syntheticpage.cpp
    yomitanfixture.cpp
)
set_target_properties(marupop_testsupport PROPERTIES AUTOMOC ON)

# PUBLIC so a suite reaches "privatebus.h" and "fakes.h" by name alone, matching the way the
# suites include the production headers.
target_include_directories(marupop_testsupport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# KF6::Archive is PUBLIC because yomitanfixture.cpp packs the .zip fixtures with KZip, and a
# suite that links this library must resolve it too.
target_link_libraries(marupop_testsupport PUBLIC marupop_lib Qt6::Core Qt6::Gui Qt6::DBus KF6::Archive)

# SyntheticPage is the one double with geometry of its own to get wrong, and every suite that hit
# tests against it inherits that geometry. QImage, QPainter and QFont need a QGuiApplication, so the
# suite provides its own main().
maru_add_gtest_app(syntheticpage_test syntheticpage_test.cpp)
target_link_libraries(syntheticpage_test PRIVATE marupop_testsupport)
