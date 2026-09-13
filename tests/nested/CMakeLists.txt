# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# The suite that only means anything inside a compositor. It is registered twice: once as a
# plain test, where every case self-skips under the offscreen platform plugin and the run costs
# 20 ms, and once through the harness, where the cases execute against kwin_wayland on a virtual
# output.
#
# AUTHORIZE is required: KWin refuses org.kde.KWin.ScreenShot2 to a caller whose executable
# matches no installed desktop entry, and the harness writes that entry into the session HOME.
maru_add_gtest_app(pipeline_test pipeline_test.cpp)
# LayerShellQt::Interface and Qt6::Widgets are already PUBLIC on marupop_lib, so only the test
# support library is added here.
target_link_libraries(pipeline_test PRIVATE marupop_testsupport)

maru_add_nested_test(pipeline_test_nested BINARY pipeline_test AUTHORIZE)
