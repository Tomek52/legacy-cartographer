include(GoogleTest)

# One executable for all unit tests while the project is small.
add_executable(lcart_unit_tests
    headers_self_contained_test.cpp
    analysis/module_rules_test.cpp
    cli/command_line_test.cpp
    cli/git_exposure_test.cpp
    config/path_glob_test.cpp
    config/project_config_test.cpp
    facts/fact_export_test.cpp
    facts/sqlite_fact_store_test.cpp
    verify/verification_status_test.cpp)
# SQLite directly: the schema-version tests tamper with a database.
target_link_libraries(lcart_unit_tests PRIVATE
    lcart::analysis lcart::cli lcart::report lcart::handoff lcart::llm
    SQLite::SQLite3 GTest::gtest_main)
lcart_target_warnings(lcart_unit_tests)
gtest_discover_tests(lcart_unit_tests PROPERTIES LABELS unit)

# Analysis against real Clang: slower, own executable and label.
add_executable(lcart_analysis_tests
    analysis/argument_filter_test.cpp
    analysis/call_edges_test.cpp
    analysis/clang_analyzer_test.cpp
    analysis/function_facts_test.cpp
    analysis/modules_test.cpp)
target_link_libraries(lcart_analysis_tests PRIVATE lcart::analysis GTest::gtest_main)
target_compile_definitions(lcart_analysis_tests PRIVATE
    LCART_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
lcart_target_warnings(lcart_analysis_tests)
gtest_discover_tests(lcart_analysis_tests PROPERTIES LABELS analysis)

# The lcart executable end to end on the fixtures (FR-006): starts the
# binary, so it gets its own executable and label.
add_executable(lcart_cli_tests cli/lcart_cli_test.cpp)
target_link_libraries(lcart_cli_tests PRIVATE GTest::gtest_main)
target_compile_definitions(lcart_cli_tests PRIVATE
    LCART_EXECUTABLE="$<TARGET_FILE:lcart>"
    LCART_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    LCART_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
add_dependencies(lcart_cli_tests lcart)
lcart_target_warnings(lcart_cli_tests)
gtest_discover_tests(lcart_cli_tests PROPERTIES LABELS cli)
