cmake_minimum_required(VERSION 3.25)

project(legacy_cartographer
    VERSION 0.0.1
    DESCRIPTION "Static-analysis cartographer for C/C++ codebases with verified LLM documentation"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LCART_BUILD_TESTS "Build the GoogleTest suites" ON)
option(LCART_BUILD_FUZZERS "Build libFuzzer targets (Clang only)" OFF)
option(LCART_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
set(LCART_SANITIZERS "" CACHE STRING "Semicolon-separated -fsanitize= values, e.g. address;undefined")

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(CompilerOptions)
include(Sanitizers)
include(ArchitectureRules)

# Third-party dependencies: from Conan (CMakeDeps) or from distribution
# packages (the *-system presets). Both provide the same imported targets.
find_package(SQLite3 REQUIRED)
find_package(nlohmann_json 3.11 REQUIRED)

add_subdirectory(src)

# Deterministic modules must never depend on the LLM layer (NFR-001, ADR-0004).
lcart_check_architecture()

if(LCART_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_subdirectory(tests)
endif()

if(LCART_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()
