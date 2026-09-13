// SPDX-FileCopyrightText: 2026 marunine
// SPDX-License-Identifier: LGPL-3.0-only
// Sets the two things every test binary needs before it reads a configuration file: a HOME of
// its own, and QStandardPaths test mode. Both are set before main() runs, and so before
// QStandardPaths answers anything.
//
// HOME: tests/CMakeLists.txt sets the same value through ctest's ENVIRONMENT property, which
// covers a `ctest` run and nothing else. A binary started straight from the build directory --
// which is how a --gtest_filter pass over one failing case is run -- would otherwise land on
// the tester's own ~/.qttest, write settings there, and hand the next suite whatever it left
// behind. Linking this file into every test target closes that path.
//
// Test mode: a suite that takes main() from GTest::gtest_main or QTEST_MAIN has no place to
// call QStandardPaths::setTestModeEnabled(true). Setting it here gives the rule one
// implementation and makes it hold for every binary, including a suite added later that
// reaches QStandardPaths through a store it only meant to construct.
#include <QStandardPaths>
#include <QtEnvironmentVariables>

namespace
{

// A namespace-scope initializer, because the plain maru_add_gtest() suites take their main()
// from GTest::gtest_main and have no earlier point to run at. QStandardPaths reads its
// test-mode flag on each call rather than caching a path at load time, so a dynamic
// initializer is early enough.
[[maybe_unused]] const bool environmentPrepared = [] {
    // Overwrites, so a HOME ctest already set to the same directory stays as it is, and a
    // HOME inherited from the tester's session is replaced rather than trusted.
    const bool homeRedirected = qputenv("HOME", MARUPOP_TEST_HOME);
    // Redirects the config, data, cache and state locations to $HOME/.qttest, which is what
    // makes the HOME above cover KConfig as well as the paths the code resolves by hand.
    QStandardPaths::setTestModeEnabled(true);
    return homeRedirected;
}();

} // namespace
