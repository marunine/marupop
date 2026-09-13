# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# The Manage Dictionaries dialog and its sub-dialogs. Every suite builds widgets and drives a
# dict::DictionaryManager rooted under its own test HOME, so each supplies its own main() with a
# QApplication in it. QT_QPA_PLATFORM=offscreen comes from maru_test_environment().
#
# Two of them read the Yomitan fixture the dict module ships, through the same
# MARUPOP_DICT_TEST_DATA_DIR the dict suites use: the folder is read-only and the import writes
# into the suite's own temporary directory.
set(MARUPOP_DICT_TEST_DATA "${CMAKE_SOURCE_DIR}/tests/data/dict")

maru_add_gtest_app(dictui_dictionarymodel_test dictionarymodel_test.cpp)

maru_add_gtest_app(dictui_dictionarymanagerdialog_test dictionarymanagerdialog_test.cpp)
target_compile_definitions(dictui_dictionarymanagerdialog_test
                           PRIVATE MARUPOP_DICT_TEST_DATA_DIR="${MARUPOP_DICT_TEST_DATA}")

# The suite packs the Yomitan fixture into a .zip through tests/support/yomitanfixture.h.
maru_add_gtest_app(dictui_adddictionarydialog_test adddictionarydialog_test.cpp)
target_compile_definitions(dictui_adddictionarydialog_test
                           PRIVATE MARUPOP_DICT_TEST_DATA_DIR="${MARUPOP_DICT_TEST_DATA}")
target_link_libraries(dictui_adddictionarydialog_test PRIVATE marupop_testsupport)

maru_add_gtest_app(dictui_configuredictionarydialog_test configuredictionarydialog_test.cpp)
maru_add_gtest_app(dictui_addcustomentrydialog_test addcustomentrydialog_test.cpp)
