# Include side of the architecture rules (B-04); cmake/ArchitectureRules.cmake
# covers linking. Run as a CTest test (label `architecture`):
#   cmake -DLCART_ROOT=<tree> -P cmake/CheckIncludeBoundaries.cmake
# Reports every violation in <tree>/src as `path:line: includes header (rule)`
# and fails if there is one.
#
# - The deterministic core never includes the LLM layer (NFR-001, ADR-0004).
# - Clang and LLVM headers stay in src/analysis, so no Clang type reaches
#   another module's interface (ADR-0002).
#
# Every `#include` line is checked, also inside `#if 0` or comments: a false
# alarm is cheaper than a missed include.

cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED LCART_ROOT)
    message(FATAL_ERROR "usage: cmake -DLCART_ROOT=<tree> -P CheckIncludeBoundaries.cmake")
endif()
get_filename_component(LCART_ROOT "${LCART_ROOT}" ABSOLUTE)

# The deterministic modules of CLAUDE.md "Boundaries".
set(deterministic_modules common config facts analysis claims verify report handoff)

file(GLOB_RECURSE sources RELATIVE "${LCART_ROOT}" LIST_DIRECTORIES false
    "${LCART_ROOT}/src/*.h" "${LCART_ROOT}/src/*.hpp"
    "${LCART_ROOT}/src/*.c" "${LCART_ROOT}/src/*.cpp")
list(SORT sources)
if(NOT sources)
    # A wrong root must not pass as "no violations".
    message(FATAL_ERROR "no C or C++ sources under ${LCART_ROOT}/src")
endif()

set(violations "")
foreach(source IN LISTS sources)
    string(REGEX MATCH "^src/([^/]+)/" ignored "${source}")
    set(module "${CMAKE_MATCH_1}")
    # One list element per line, empty lines kept, so line numbers are
    # right: `;` and brackets would split or join list elements, and no
    # include directive needs them.
    file(READ "${LCART_ROOT}/${source}" content)
    string(REGEX REPLACE "[][;\\]" " " content "${content}")
    string(REPLACE "\n" ";" lines "${content}")
    set(line_number 0)
    foreach(line IN LISTS lines)
        math(EXPR line_number "${line_number} + 1")
        if(NOT line MATCHES "^[ \t]*#[ \t]*include[ \t]*[<\"]([^>\"]+)[>\"]")
            continue()
        endif()
        set(header "${CMAKE_MATCH_1}")
        set(rule "")
        if(header MATCHES "^llm/" AND module IN_LIST deterministic_modules)
            set(rule "the deterministic core must not include the LLM layer, NFR-001")
        elseif(header MATCHES "^(clang|clang-c|llvm|llvm-c)/" AND NOT module STREQUAL "analysis")
            set(rule "Clang and LLVM headers stay in src/analysis, ADR-0002")
        endif()
        if(rule)
            string(APPEND violations "  ${source}:${line_number}: includes ${header} (${rule})\n")
        endif()
    endforeach()
endforeach()

if(violations)
    message(FATAL_ERROR "Include boundary violated:\n${violations}")
endif()
list(LENGTH sources count)
message(STATUS "Include boundaries hold in ${count} files under ${LCART_ROOT}/src")
