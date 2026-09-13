# SPDX-FileCopyrightText: 2026 marunine
# SPDX-License-Identifier: LGPL-3.0-only
maru_add_gtest(ruleset_test ruleset_test.cpp)
# deconjugation_cases.h holds the 5,224 rows tools/gen-deconj-tests.py scrapes out of JL's NUnit
# suite. It is checked in rather than generated at build time because the generator reads a JL
# checkout, which is not a build dependency. The suite reads the rule set through the Qt
# resource, so it needs no fixture files.
maru_add_gtest(deconjugator_test deconjugator_test.cpp deconjugation_cases.h)
target_include_directories(deconjugator_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
