include(GoogleTest)

# One executable for all unit tests while the project is small.
add_executable(lcart_unit_tests
    headers_self_contained_test.cpp
    verify/verification_status_test.cpp)
target_link_libraries(lcart_unit_tests PRIVATE
    lcart::analysis lcart::report lcart::llm
    GTest::gtest_main)
lcart_target_warnings(lcart_unit_tests)
gtest_discover_tests(lcart_unit_tests PROPERTIES LABELS unit)
