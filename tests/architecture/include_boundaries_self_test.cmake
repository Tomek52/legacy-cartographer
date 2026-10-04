# Self-test of cmake/CheckIncludeBoundaries.cmake (B-04 criterion 2): run on
# the fixture tree, the check must fail and name exactly the two forbidden
# includes, not the Clang include inside src/analysis.
execute_process(
    COMMAND ${CMAKE_COMMAND} -DLCART_ROOT=${LCART_FIXTURE} -P ${LCART_CHECK}
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
if(status EQUAL 0)
    message(FATAL_ERROR "the check passed on the fixture tree:\n${output}")
endif()
foreach(expected
        "src/report/uses_llm.cpp:3: includes llm/provider.hpp"
        "src/common/uses_clang.hpp:5: includes clang/AST/Decl.h")
    string(FIND "${output}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "missing \"${expected}\" in:\n${output}")
    endif()
endforeach()
string(FIND "${output}" "allowed.cpp" found)
if(NOT found EQUAL -1)
    message(FATAL_ERROR "the allowed include in src/analysis was reported:\n${output}")
endif()
