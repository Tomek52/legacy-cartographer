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

# Clang/LLVM 20 from the distribution packages (ADR-0002, ADR-0007), never
# from Conan. On Ubuntu: llvm-20-dev libclang-20-dev libclang-cpp20-dev.
# LLVMConfigVersion.cmake accepts only an exact major.minor request, so the
# package is found by path and the major version checked here.
find_package(LLVM REQUIRED CONFIG HINTS /usr/lib/llvm-20/lib/cmake/llvm NO_DEFAULT_PATH)
if(NOT LLVM_VERSION_MAJOR EQUAL 20)
    message(FATAL_ERROR "LLVM 20 required (ADR-0007), found ${LLVM_PACKAGE_VERSION} in ${LLVM_DIR}")
endif()
find_package(Clang REQUIRED CONFIG HINTS "${LLVM_DIR}/../clang" NO_DEFAULT_PATH)
set(LCART_CLANG_RESOURCE_DIR "${LLVM_LIBRARY_DIR}/clang/${LLVM_VERSION_MAJOR}")
if(NOT EXISTS "${LCART_CLANG_RESOURCE_DIR}/include/stddef.h")
    message(FATAL_ERROR "Clang resource directory not found at ${LCART_CLANG_RESOURCE_DIR}")
endif()
message(STATUS "LibTooling: LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")

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
