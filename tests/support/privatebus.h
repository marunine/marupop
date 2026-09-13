// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// The gate every fake D-Bus service in tests/support/ passes before it registers a name.
//
// The fakes claim the well-known names the production code looks up: org.kde.KWin and
// org.freedesktop.ScreenSaver. Claiming either on the session bus of a logged-in Plasma session
// would divert calls from other processes, so registration is allowed on a bus that
// dbus-run-session started for one test binary alone. maru_add_gtest_dbus() in
// tests/CMakeLists.txt sets MARUPOP_TEST_PRIVATE_BUS=1 on exactly those runs, and this header
// treats that variable as the only evidence of a private bus. A binary started by hand without
// it reports every fake as unavailable and its cases skip.
#pragma once

#include <QString>

namespace maru::test
{

// True where the current process runs on a session bus of its own, which is where a fake may
// register a well-known name.
[[nodiscard]] bool privateBusAvailable();

// The sentence a GTEST_SKIP() prints where privateBusAvailable() is false, naming the variable
// and the CMake function that sets it.
[[nodiscard]] QString privateBusSkipReason();

} // namespace maru::test
