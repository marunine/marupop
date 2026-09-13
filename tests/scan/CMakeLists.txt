# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# Pure logic, no Q*Application needed.
maru_add_gtest(scancache_test scancache_test.cpp)

# QGuiApplication and an event loop: the throttle delivers its trailing fire through a QTimer,
# KSystemClipboard needs a GUI application behind it, and the controller reads QScreen geometry
# and takes every reply of the loop through the event loop. Each suite provides its own main(),
# so maru_add_gtest_app rather than maru_add_gtest.
maru_add_gtest_app(throttle_test throttle_test.cpp)
maru_add_gtest_app(wordcopier_test wordcopier_test.cpp)
maru_add_gtest_app(scancontroller_test scancontroller_test.cpp)
# The scan ladder against the extent a character reaches the detector input at, over a page fixed
# on the desktop. Its geometry is its own, so it is a second file rather than a case in
# scancontroller_test.cpp, whose helpers anchor the text to the grabbed frame.
maru_add_gtest_app(scanresolution_test scanresolution_test.cpp)
# The pointer-to-anchor lag of the whole loop, over a page fixed on the desktop rather than on
# the grabbed frame. It prints a table and asserts the shape of the two regimes.
maru_add_gtest_app(trackinglatency_test trackinglatency_test.cpp)
# FakeTracker, FakeLock and FakeBackend.
target_link_libraries(scancontroller_test PRIVATE marupop_testsupport)
target_link_libraries(scanresolution_test PRIVATE marupop_testsupport)
target_link_libraries(trackinglatency_test PRIVATE marupop_testsupport)
# The same binary a second time on the harness's 1920x1080 virtual output. It needs no compositor
# service, only a workspace the size of a real desktop: the region ladder, the number of
# characters a sweep crosses and the distance the pointer travels all scale with it, so the
# offscreen 800x800 run and this one are the two ends of the range the numbers hold over.
maru_add_nested_test(trackinglatency_test_nested BINARY trackinglatency_test)
