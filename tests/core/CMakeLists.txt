# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
maru_add_gtest(logging_test logging_test.cpp)
# Reads the .categories file ecm_qt_install_logging_categories() generates, which is the file
# installed for kdebugsettings, and scans src/ for the Q_LOGGING_CATEGORY macros it has to
# agree with. Reading the generated file is what makes the suite fail on a category defined in
# core/logging.cpp without a matching registration in src/CMakeLists.txt.
target_compile_definitions(logging_test
                           PRIVATE MARUPOP_CATEGORIES_PATH="${CMAKE_BINARY_DIR}/src/marupop.categories"
                                   MARUPOP_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src")

maru_add_gtest(paths_test paths_test.cpp)
# PopSettings::self() builds a KConfig, which needs a QCoreApplication for the application
# name that names marupoprc.
maru_add_gtest_app(settings_test settings_test.cpp)
