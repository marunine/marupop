# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# Pure geometry, no Q*Application needed.
maru_add_gtest(screenlayout_test screenlayout_test.cpp)
maru_add_gtest(scanregion_test scanregion_test.cpp)
# The tile grid a zwlr_screencopy_v1 region grab snaps to, which is what bounds the screenshare
# sessions Hyprland retains per distinct capture box.
maru_add_gtest(wlrtile_test wlrtile_test.cpp)
# Which of Hyprland's two configuration languages the advice is written in. Pure environment and
# filesystem reads, so no Q*Application; the suite sets the four variables it depends on itself.
maru_add_gtest(hyprlandconfig_test hyprlandconfig_test.cpp)

# QGuiApplication and an event loop: the fake source answers through the event loop, and the
# live case reads QScreen geometry and waits for a D-Bus reply. The suite provides its own
# main(), so maru_add_gtest_app rather than maru_add_gtest.
#
# The live case is gated twice, on org.kde.KWin owning its bus name and on
# MARUPOP_LIVE_CAPTURE=1, and skips with a pointer at tools/install-dev-desktop.sh where KWin
# denies the grab.
maru_add_gtest_app(framesource_test framesource_test.cpp)
# maru::test::waitFor, which every suite waiting on an asynchronous reply shares.
target_link_libraries(framesource_test PRIVATE marupop_testsupport)

# The same live case against a real compositor, without a logged-in Plasma session: the harness
# starts kwin_wayland on a virtual output and writes the desktop entry that authorizes this
# binary to call org.kde.KWin.ScreenShot2.
maru_add_nested_test(framesource_test_nested
    BINARY framesource_test
    AUTHORIZE
    ENVIRONMENT MARUPOP_LIVE_CAPTURE=1
)

# The ScreenShot2 client against a fake compositor on a private bus, which is what makes the
# NoAuthorized, Cancelled and short-pipe replies reachable.
maru_add_gtest_dbus(kwingrabber_test kwingrabber_test.cpp)

# The desktop-entry gate, which reads the service catalog in the suite's own HOME.
maru_add_gtest_app(authorization_test authorization_test.cpp)
# KSycoca is stated here rather than inherited. src/CMakeLists.txt links KF6::Service PRIVATE on
# marupop_lib, so this suite's <KSycoca> include arrives only through KF6::KIOGui's interface, and
# narrowing a KIO dependency would break it with an error naming neither cause.
target_link_libraries(authorization_test PRIVATE KF6::Service)
