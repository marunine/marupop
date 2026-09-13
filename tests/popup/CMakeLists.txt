# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# placement_test and renderer_test are pure string and geometry logic and need no
# Q*Application. The other four build a QFont, a QTextDocument or a widget, each of which
# needs a QApplication, so they supply their own main(). QT_QPA_PLATFORM=offscreen comes from
# maru_test_environment().
maru_add_gtest(placement_test placement_test.cpp)
maru_add_gtest(renderer_test renderer_test.cpp)
maru_add_gtest_app(theme_test theme_test.cpp)
maru_add_gtest_app(pitchpainter_test pitchpainter_test.cpp)
maru_add_gtest_app(popupview_test popupview_test.cpp)
maru_add_gtest_app(popupwindow_test popupwindow_test.cpp)
target_link_libraries(popupwindow_test PRIVATE marupop_testsupport)

# The same suite inside a nested compositor, where QGuiApplication::platformName() starts with
# "wayland" and PopupWindow::isLayerShellSession() is therefore true. Under the offscreen plugin
# the registration above takes the fallback geometry path instead, so the zwlr_layer_shell_v1
# branch every real session executes is covered only here. AUTHORIZE lets the suite read the card
# back through org.kde.KWin.ScreenShot2, which is where the compositor's placement is observable
# rather than the margins the client asked for.
maru_add_nested_test(popupwindow_test_nested BINARY popupwindow_test AUTHORIZE)

# The same suite again on two virtual outputs, which KWin lays out left to right. It is what runs
# rebuildsTheSurfaceOnTheScreenThePointerCrossedOnto: a zwlr_layer_surface_v1 is bound to one
# output for its lifetime, so a card that crosses a monitor boundary is only observable where
# there is a second monitor to cross onto.
maru_add_nested_test(popupwindow_test_dual_nested BINARY popupwindow_test AUTHORIZE OUTPUTS 2)
