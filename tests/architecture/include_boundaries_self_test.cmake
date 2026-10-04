# Self-test of cmake/CheckIncludeBoundaries.cmake (B-04 criterion 2): run on
# the fixture tree, the check must fail and name exactly the four forbidden
# includes, not the Clang include inside src/analysis.
#
# Verifies: b04-include-boundary-check-v1 (criterion 2).
execute_process(
    COMMAND ${CMAKE_COMMAND} -DLCART_ROOT=${LCART_FIXTURE} -P ${LCART_CHECK}
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
if(status EQUAL 0)
    message(FATAL_ERROR "the check passed on the fixture tree:\n${output}")
endif()
set(expected_violations
    "src/common/uses_clang.hpp:5: includes clang/AST/Decl.h"
    "src/report/src/other_spellings.cc:2: includes ../../llm/provider.hpp"
    "src/report/src/other_spellings.cc:3: includes llvm/Support/raw_ostream.h"
    "src/report/uses_llm.cpp:3: includes llm/provider.hpp")
foreach(expected IN LISTS expected_violations)
    string(FIND "${output}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "missing \"${expected}\" in:\n${output}")
    endif()
endforeach()
# Nothing else: in particular not the allowed include in src/analysis.
string(REGEX MATCHALL ": includes " reported "${output}")
list(LENGTH reported reported_count)
list(LENGTH expected_violations expected_count)
if(NOT reported_count EQUAL expected_count)
    message(FATAL_ERROR
        "${reported_count} violations reported, expected ${expected_count}:\n${output}")
endif()
