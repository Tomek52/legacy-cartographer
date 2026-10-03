# Build-time fitness function: the deterministic core must work without any
# LLM code linked in (NFR-001, ADR-0004). Checked at configure time, so a
# forbidden target_link_libraries() fails `cmake --preset ...` immediately.

set(LCART_DETERMINISTIC_MODULES
    lcart_common lcart_facts lcart_analysis lcart_claims lcart_verify lcart_report)
set(LCART_FORBIDDEN_IN_DETERMINISTIC lcart_llm)

# Appends every target reachable from `target` through INTERFACE_LINK_LIBRARIES
# to the list variable named by `out_var`.
function(_lcart_collect_link_closure target out_var)
    set(closure ${${out_var}})
    get_target_property(deps ${target} INTERFACE_LINK_LIBRARIES)
    if(deps)
        foreach(dep IN LISTS deps)
            if(NOT TARGET ${dep})
                continue()
            endif()
            get_target_property(aliased ${dep} ALIASED_TARGET)
            if(aliased)
                set(dep ${aliased})
            endif()
            if(NOT dep IN_LIST closure)
                list(APPEND closure ${dep})
                _lcart_collect_link_closure(${dep} closure)
            endif()
        endforeach()
    endif()
    set(${out_var} ${closure} PARENT_SCOPE)
endfunction()

function(lcart_check_architecture)
    foreach(module IN LISTS LCART_DETERMINISTIC_MODULES)
        set(closure "")
        _lcart_collect_link_closure(${module} closure)
        foreach(forbidden IN LISTS LCART_FORBIDDEN_IN_DETERMINISTIC)
            if(forbidden IN_LIST closure)
                message(FATAL_ERROR
                    "Architecture rule violated: ${module} links ${forbidden}. "
                    "The deterministic core must not depend on the LLM layer "
                    "(NFR-001, docs/adr/0004-llm-provider-abstraction-and-code-privacy.md).")
            endif()
        endforeach()
    endforeach()
endfunction()
