# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# The lookup suites. Both read their fixtures from the source tree through
# MARUPOP_DICT_TEST_DATA_DIR and MARUPOP_LOOKUP_TEST_DATA_DIR and import them into a
# QTemporaryDir of their own, so nothing is written next to the fixtures.
#
# Both need a QCoreApplication: the suites build a dict::DictionaryManager, which is a QObject,
# and the real-data suite reads the settings singleton.
set(MARUPOP_LOOKUP_TEST_DATA "${CMAKE_SOURCE_DIR}/tests/data/lookup")
set(MARUPOP_DICT_TEST_DATA "${CMAKE_SOURCE_DIR}/tests/data/dict")

function(maru_add_lookup_test name)
    maru_add_gtest_app(${name} ${ARGN})
    target_compile_definitions(
        ${name} PRIVATE MARUPOP_DICT_TEST_DATA_DIR="${MARUPOP_DICT_TEST_DATA}"
                        MARUPOP_LOOKUP_TEST_DATA_DIR="${MARUPOP_LOOKUP_TEST_DATA}")
endfunction()

maru_add_lookup_test(lookup_test lookup_test.cpp)

# The engine answering on QThreadPool threads while the GUI thread mutates the dictionary list.
# Meaningful under -DECM_ENABLE_SANITIZERS='address;undefined'; bounded to 200 lookups and a 60 s
# deadline so a CI run is deterministic.
maru_add_lookup_test(lookup_concurrency_test concurrency_test.cpp)
set_tests_properties(lookup_concurrency_test PROPERTIES TIMEOUT 300)

# Optional imports from the configured JMdict fixture plus the hover benchmark.
# Requires MARUPOP_REAL_DATA=1; a cold run imports the dictionary into the test store.
maru_add_lookup_test(lookup_realdata_test realdata_test.cpp)
set_tests_properties(lookup_realdata_test PROPERTIES TIMEOUT 3600)

# The recognition-variant pass over its own mock JMdict (tests/data/lookup/mock_variants.xml),
# kept apart from lookup_test so the JL port's expectations run over JL's fixture alone.
maru_add_lookup_test(lookup_ocrvariants_test ocrvariants_test.cpp)
