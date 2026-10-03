add_executable(lcart main.cpp)
target_link_libraries(lcart PRIVATE
    lcart::analysis lcart::report lcart::handoff lcart::llm)
lcart_target_warnings(lcart)
