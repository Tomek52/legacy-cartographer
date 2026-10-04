# A module without sources is an INTERFACE library (headers only); it
# becomes a STATIC library when its backlog item adds sources (SOURCES).
# Headers are included relative to src/: #include "facts/fact_store.hpp".

function(lcart_add_module name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS;PRIVATE_DEPS" ${ARGN})
    if(ARG_SOURCES)
        add_library(lcart_${name} STATIC ${ARG_SOURCES})
        set(scope PUBLIC)
        lcart_target_warnings(lcart_${name})
    else()
        add_library(lcart_${name} INTERFACE)
        set(scope INTERFACE)
    endif()
    add_library(lcart::${name} ALIAS lcart_${name})
    target_include_directories(lcart_${name} ${scope} "${PROJECT_SOURCE_DIR}/src")
    target_compile_features(lcart_${name} ${scope} cxx_std_23)
    if(ARG_DEPS)
        target_link_libraries(lcart_${name} ${scope} ${ARG_DEPS})
    endif()
    if(ARG_PRIVATE_DEPS)
        target_link_libraries(lcart_${name} PRIVATE ${ARG_PRIVATE_DEPS})
    endif()
endfunction()

lcart_add_module(common)
target_compile_definitions(lcart_common INTERFACE LCART_VERSION="${PROJECT_VERSION}")

# SQLite stays private to facts: other modules see only FactWriter/FactQuery.
lcart_add_module(facts
    SOURCES facts/src/fact_export.cpp facts/src/sqlite_fact_store.cpp
    DEPS lcart::common
    PRIVATE_DEPS SQLite::SQLite3)
# lcart.toml and its path globs (ADR-0008, ADR-0014); toml++ stays private.
lcart_add_module(config
    SOURCES config/src/path_glob.cpp config/src/project_config.cpp
    DEPS lcart::common
    PRIVATE_DEPS lcart::tomlplusplus)
# Clang LibTooling stays private to analysis (ADR-0002): no other module
# sees a Clang header or links Clang.
lcart_add_module(analysis
    SOURCES analysis/src/argument_filter.cpp analysis/src/clang_analyzer.cpp
            analysis/src/call_fact_collector.cpp analysis/src/file_fact_collector.cpp
            analysis/src/function_fact_collector.cpp
            analysis/src/module_rules.cpp
    DEPS lcart::facts lcart::config
    PRIVATE_DEPS clang-cpp LLVM)
# Clang's builtin headers (stddef.h, ...) live in its resource directory,
# which LibTooling would otherwise look for next to the lcart executable.
target_compile_definitions(lcart_analysis PRIVATE
    LCART_CLANG_RESOURCE_DIR="${LCART_CLANG_RESOURCE_DIR}")
# The imported clang-cpp/LLVM targets carry no include directories. SYSTEM
# keeps our warning flags (-Werror) away from LLVM's headers.
target_include_directories(lcart_analysis SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
separate_arguments(_lcart_llvm_definitions NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(lcart_analysis PRIVATE ${_lcart_llvm_definitions})
lcart_add_module(claims   DEPS lcart::common nlohmann_json::nlohmann_json)
lcart_add_module(verify   DEPS lcart::claims lcart::facts)
lcart_add_module(report   DEPS lcart::verify)
lcart_add_module(handoff  DEPS lcart::claims lcart::facts)
lcart_add_module(llm      DEPS lcart::common)  # built-in providers, M6 (ADR-0006)
# Command-line parsing and the commands, as a library so tests can call
# them without starting the executable.
lcart_add_module(cli
    SOURCES cli/src/command_line.cpp cli/src/commands.cpp cli/src/git_exposure.cpp
    DEPS lcart::analysis lcart::config lcart::facts)

add_subdirectory(cli)
