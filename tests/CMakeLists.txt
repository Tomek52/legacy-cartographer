include(GoogleTest)

# One executable for all unit tests while the project is small.
add_executable(lcart_unit_tests
    headers_self_contained_test.cpp
    analysis/module_rules_test.cpp
    facts/sqlite_fact_store_test.cpp
    verify/verification_status_test.cpp)
# SQLite directly: the schema-version tests tamper with a database.
target_link_libraries(lcart_unit_tests PRIVATE
    lcart::analysis lcart::report lcart::handoff lcart::llm
    SQLite::SQLite3 GTest::gtest_main)
lcart_target_warnings(lcart_unit_tests)
gtest_discover_tests(lcart_unit_tests PROPERTIES LABELS unit)

# Analysis against real Clang: slower, own executable and label.
add_executable(lcart_analysis_tests
    analysis/argument_filter_test.cpp
    analysis/call_edges_test.cpp
    analysis/clang_analyzer_test.cpp
    analysis/function_facts_test.cpp)
target_link_libraries(lcart_analysis_tests PRIVATE lcart::analysis GTest::gtest_main)
target_compile_definitions(lcart_analysis_tests PRIVATE
    LCART_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
lcart_target_warnings(lcart_analysis_tests)
gtest_discover_tests(lcart_analysis_tests PROPERTIES LABELS analysis)
