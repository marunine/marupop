# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
# Pure-logic suites: geometry, the wire format and the grouping rules, none of which needs a
# Q*Application.
maru_add_gtest(ocr_meikipreprocess_test meikipreprocess_test.cpp)
maru_add_gtest(ocr_meikipostprocess_test meikipostprocess_test.cpp)
maru_add_gtest(ocr_grouping_test grouping_test.cpp)
maru_add_gtest(ocr_hittest_test hittest_test.cpp)
maru_add_gtest(ocr_resolution_test resolution_test.cpp)
maru_add_gtest(ocr_screenaiproto_test screenaiproto_test.cpp)

# PopSettings, QNetworkAccessManager and the OCR worker thread each need a QCoreApplication,
# and the live backend cases need a QApplication for font rendering.
maru_add_gtest_app(ocr_modelstore_test modelstore_test.cpp)
maru_add_gtest_app(ocr_service_test ocrservice_test.cpp)
# FakeBackend.
target_link_libraries(ocr_service_test PRIVATE marupop_testsupport)
maru_add_gtest_app(ocr_backend_test ocrbackend_test.cpp)
# The hit-test accuracy of the real detector against the extent one character reaches its input
# at, over a rendered page. Skips where the models or a Japanese font are absent.
maru_add_gtest_app(ocr_detectorresolution_test detectorresolution_test.cpp)
target_link_libraries(ocr_detectorresolution_test PRIVATE marupop_testsupport)

# The post-recognition rules, including a replay of the standalone scalar reference interpreter over
# tests/data/ocr/meikicorrections_cases.json (tools/gen-meiki-correction-cases.py).
maru_add_gtest(ocr_meikicorrections_test meikicorrections_test.cpp)
target_compile_definitions(ocr_meikicorrections_test
                           PRIVATE MARUPOP_OCR_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data/ocr")
