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
# Every line that starts with an `#include` or `#include_next` directive is
# checked, also inside `#if 0` or a block comment: a false alarm is cheaper
# than a missed include. A header matches by any path segment (`llm/`,
# `../llm/`, `report/../llm/`), for the same reason. Not detected: an
# include through a macro (`#include HEADER`); none exists in src/.
#
# Verifies: b04-include-boundary-check-v1 (criteria 1 and 3),
# nfr001-works-without-llm-v1, adr0002-isolated-in-analysis-v1.

cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED LCART_ROOT)
    message(FATAL_ERROR "usage: cmake -DLCART_ROOT=<tree> -P CheckIncludeBoundaries.cmake")
endif()
get_filename_component(LCART_ROOT "${LCART_ROOT}" ABSOLUTE)

# The deterministic modules of CLAUDE.md "Boundaries".
set(deterministic_modules common config facts analysis claims verify report handoff)

file(GLOB_RECURSE sources RELATIVE "${LCART_ROOT}" LIST_DIRECTORIES false
    "${LCART_ROOT}/src/*.h" "${LCART_ROOT}/src/*.hh" "${LCART_ROOT}/src/*.hpp"
    "${LCART_ROOT}/src/*.hxx" "${LCART_ROOT}/src/*.ipp" "${LCART_ROOT}/src/*.inc"
    "${LCART_ROOT}/src/*.c" "${LCART_ROOT}/src/*.cc" "${LCART_ROOT}/src/*.cpp"
    "${LCART_ROOT}/src/*.cxx")
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
    # right: `;` and brackets would split or join list elements, and a
    # trailing `\` (macro continuation) would escape the next `;` and join
    # two lines. No include directive needs any of them.
    file(READ "${LCART_ROOT}/${source}" content)
    string(REGEX REPLACE "[][;\\]" " " content "${content}")
    string(REPLACE "\n" ";" lines "${content}")
    set(line_number 0)
    foreach(line IN LISTS lines)
        math(EXPR line_number "${line_number} + 1")
        if(NOT line MATCHES "^[ \t]*#[ \t]*include(_next)?[ \t]*[<\"]([^>\"]+)[>\"]")
            continue()
        endif()
        set(header "${CMAKE_MATCH_2}")
        set(rule "")
        if(header MATCHES "(^|/)llm/" AND module IN_LIST deterministic_modules)
            set(rule "the deterministic core must not include the LLM layer, NFR-001")
        elseif(header MATCHES "(^|/)(clang|clang-c|llvm|llvm-c)/" AND NOT module STREQUAL "analysis")
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
