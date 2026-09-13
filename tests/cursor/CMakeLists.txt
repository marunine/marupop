# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# All three suites need a QGuiApplication and an event loop: the sink resolves a position
# through QGuiApplication::screenAt(), and every D-Bus reply arrives through the event loop.
# Each provides its own main().
#
# All three run on a private session bus, which serves two purposes. A fake service may claim
# the well-known name the production code looks up -- org.kde.KWin for the relay,
# org.freedesktop.ScreenSaver for the lock watcher -- so the states a live session cannot be
# asked for become deterministic. And the absent-compositor cases become reachable, because
# org.kde.KWin is unowned on a bus of the suite's own.
maru_add_gtest_dbus(cursorsink_test cursorsink_test.cpp)
maru_add_gtest_dbus(kwinscriptrelay_test kwinscriptrelay_test.cpp)
maru_add_gtest_dbus(lockwatcher_test lockwatcher_test.cpp)

# The Hyprland pointer poll against maru::test::FakeHyprSocket, which listens on the path
# cursor::hyprSocketPath() builds under a temporary runtime directory. No session bus is
# involved, so this one is maru_add_gtest_app rather than maru_add_gtest_dbus; a QGuiApplication
# is still needed, because the tracker resolves a position through QGuiApplication::screenAt().
maru_add_gtest_app(hyprcursortracker_test hyprcursortracker_test.cpp)
target_link_libraries(hyprcursortracker_test PRIVATE marupop_testsupport)

# The relay against a real compositor, without a logged-in Plasma session: the harness loads the
# script package into a nested kwin_wayland and the sink receives its positions over the nested
# bus. MARUPOP_LIVE_KWIN=1 is what the suite gates that case on.
maru_add_nested_test(kwinscriptrelay_test_nested
    BINARY kwinscriptrelay_test
    ENVIRONMENT MARUPOP_LIVE_KWIN=1
)

# The sink against the nested compositor's QScreen geometry, which is a virtual 1920x1080
# output rather than the offscreen plugin's default.
maru_add_nested_test(cursorsink_test_nested BINARY cursorsink_test)
