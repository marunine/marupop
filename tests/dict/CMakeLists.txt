# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# The dictionary suites. Every one of them reads its fixtures from tests/data/dict through
# MARUPOP_DICT_TEST_DATA_DIR, which points at the source tree rather than at a copy in the build
# directory: the fixtures are read-only and each suite writes into its own QTemporaryDir.
set(MARUPOP_DICT_TEST_DATA "${CMAKE_SOURCE_DIR}/tests/data/dict")

function(maru_add_dict_test name)
    maru_add_gtest(${name} ${ARGN})
    target_compile_definitions(${name} PRIVATE MARUPOP_DICT_TEST_DATA_DIR="${MARUPOP_DICT_TEST_DATA}")
    # The store tests drive sqlite3 directly to age a database, which appears in no marupop_lib
    # header, so the link is here. marupop_testsupport carries the .zip fixture packers the Yomitan
    # test uses, and KF6::Archive with them.
    target_link_libraries(${name} PRIVATE PkgConfig::SQLITE3 marupop_testsupport)
endfunction()

maru_add_dict_test(dict_codec_test codec_test.cpp)
maru_add_dict_test(dict_keyfilter_test keyfilter_test.cpp)
maru_add_dict_test(dict_store_test store_test.cpp)
maru_add_dict_test(dict_wordclasses_test wordclasses_test.cpp)
maru_add_dict_test(dict_importers_test importers_test.cpp)
maru_add_dict_test(dict_jmdictimporter_test jmdictimporter_test.cpp)
maru_add_dict_test(dict_yomitanimporter_test yomitanimporter_test.cpp)

# The manager and the KJob it creates need a QCoreApplication and an event loop, so the suite
# supplies its own main().
maru_add_gtest_app(dict_dictionarymanager_test dictionarymanager_test.cpp)
target_compile_definitions(dict_dictionarymanager_test
                           PRIVATE MARUPOP_DICT_TEST_DATA_DIR="${MARUPOP_DICT_TEST_DATA}")

# The download and update-check jobs, over file:// URLs and one QTcpServer on 127.0.0.1 that
# answers with a body it never finishes. KCompressionDevice writes the gzipped fixture the job
# decompresses, and Qt6::Network supplies the server; neither type appears in a marupop_lib
# header, so both links are here.
maru_add_gtest_app(dict_downloadjob_test downloadjob_test.cpp)
target_link_libraries(dict_downloadjob_test PRIVATE KF6::Archive Qt6::Network)

# Optional imports from configured dictionary fixtures. MARUPOP_REAL_DATA=1 enables
# them; large collections can take minutes, so these tests have a longer timeout.
maru_add_gtest_app(dict_realdata_test realdata_test.cpp)
target_compile_definitions(dict_realdata_test PRIVATE MARUPOP_DICT_TEST_DATA_DIR="${MARUPOP_DICT_TEST_DATA}")
set_tests_properties(dict_realdata_test PROPERTIES TIMEOUT 3600)
