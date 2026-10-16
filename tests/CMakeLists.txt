# tests/CMakeLists.txt -- unit tests, driven by CTest (plan.md section 13).
#
#   ctest --test-dir build --output-on-failure     # everything
#   ctest --test-dir build -R tokenizer            # a single test
#
# Each test is a standalone .cpp with its own main() checking its own cases:
# the project stays free of third-party test frameworks (plan.md 16). To add
# one, drop tests/test_xxx.cpp next to this file and append test_xxx to
# MSE_TESTS below.

set(MSE_TESTS
    test_tokenizer
    test_stemmer
    test_stopwords)

foreach(test IN LISTS MSE_TESTS)
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE mse_core mse_warnings)
    add_test(NAME ${test} COMMAND ${test})
endforeach()

# Tests are run from the project root so they can read the corpus with the
# same relative paths the CLI uses (data/manifest.tsv).
set_tests_properties(${MSE_TESTS} PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

# Smoke test: the corpus committed to the repository still loads and reports
# the 195 documents documented in README.md / plan.md.
add_test(NAME mse_stats
         COMMAND mse stats --manifest ${PROJECT_SOURCE_DIR}/data/manifest.tsv)
set_tests_properties(mse_stats PROPERTIES
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    PASS_REGULAR_EXPRESSION "documents: 195")
