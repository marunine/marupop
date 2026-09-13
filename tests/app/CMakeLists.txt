# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# Every suite here builds widgets, so every one supplies its own main() with a QApplication in
# it. QT_QPA_PLATFORM=offscreen comes from maru_test_environment().
maru_add_gtest_app(trayicon_test trayicon_test.cpp)
maru_add_gtest_app(settingsdialog_test settingsdialog_test.cpp)
maru_add_gtest_app(modeldownloaddialog_test modeldownloaddialog_test.cpp)
maru_add_gtest_app(application_test application_test.cpp)
maru_add_gtest_app(lookupwindow_test lookupwindow_test.cpp)

# No widget: the registry and the notifier are both read without one, and a QCoreApplication is
# what keeps a suite that never opens a window off the display.
maru_add_gtest_app(hotkeyregistry_test hotkeyregistry_test.cpp)
maru_add_gtest_app(notifier_test notifier_test.cpp)

# The compositor-owned shortcut registry. The two functions that build a `bind` line are pure,
# and the registration itself needs a compositor advertising
# hyprland_global_shortcuts_manager_v1, which tests/nested/ registers behind
# MARUPOP_LIVE_HYPRLAND. A QGuiApplication is needed for
# ShortcutRegistry::componentName(), which reads QGuiApplication::desktopFileName().
maru_add_gtest_app(wlrshortcuts_test wlrshortcuts_test.cpp)

# The notifyrc the data/ target writes into the build tree, which is the file installed. The
# test reads it rather than the template, so a merge that damaged it fails here.
target_compile_definitions(notifier_test
                           PRIVATE MARUPOP_NOTIFYRC_PATH="${CMAKE_BINARY_DIR}/data/marupop.notifyrc")
