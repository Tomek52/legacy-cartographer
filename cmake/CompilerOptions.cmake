# Warning flags for code owned by this repository. Applied per target so that
# third-party and analyzed code (targets/) are never built with them.
function(lcart_target_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic
        -Wconversion -Wsign-conversion
        -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual
        -Wnull-dereference -Wimplicit-fallthrough -Wswitch-enum)
    if(LCART_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()
