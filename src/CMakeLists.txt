# Each module is an INTERFACE library in M0 (headers only). A module gains
# sources, and becomes a STATIC library, when its backlog item implements it.
# Headers are included relative to src/: #include "facts/fact_store.hpp".

function(lcart_add_module name)
    cmake_parse_arguments(ARG "" "" "DEPS" ${ARGN})
    add_library(lcart_${name} INTERFACE)
    add_library(lcart::${name} ALIAS lcart_${name})
    target_include_directories(lcart_${name} INTERFACE "${PROJECT_SOURCE_DIR}/src")
    target_compile_features(lcart_${name} INTERFACE cxx_std_20)
    if(ARG_DEPS)
        target_link_libraries(lcart_${name} INTERFACE ${ARG_DEPS})
    endif()
endfunction()

lcart_add_module(common)
target_compile_definitions(lcart_common INTERFACE LCART_VERSION="${PROJECT_VERSION}")

lcart_add_module(facts    DEPS lcart::common SQLite::SQLite3)
lcart_add_module(analysis DEPS lcart::facts)   # + Clang LibTooling in B-07
lcart_add_module(claims   DEPS lcart::common nlohmann_json::nlohmann_json)
lcart_add_module(verify   DEPS lcart::claims lcart::facts)
lcart_add_module(report   DEPS lcart::verify)
lcart_add_module(llm      DEPS lcart::common)  # HTTP client chosen in M2 (open question)

add_subdirectory(cli)
