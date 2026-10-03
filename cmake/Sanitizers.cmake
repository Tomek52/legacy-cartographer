# Sanitizers apply to the whole build tree so that headers instantiated in
# tests are instrumented too. Dependencies from Conan are not rebuilt with
# them; ASan/UBSan tolerate that, MSan would not (not supported here).
if(LCART_SANITIZERS)
    list(JOIN LCART_SANITIZERS "," _lcart_sanitizers)
    add_compile_options(
        -fsanitize=${_lcart_sanitizers}
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all)
    add_link_options(-fsanitize=${_lcart_sanitizers})
endif()
