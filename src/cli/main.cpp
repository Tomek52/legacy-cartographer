#include "cli/command_line.hpp"
#include "cli/commands.hpp"

#include <cstddef>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    const std::span program_arguments(argv, static_cast<std::size_t>(argc));
    std::vector<std::string_view> arguments;
    for (const char* argument : program_arguments.subspan(1)) {
        arguments.emplace_back(argument);
    }
    const auto command = lcart::cli::parse_command_line(arguments);
    if (!command) {
        std::cerr << "lcart: " << command.error().message
                  << "\n(lcart --help lists the commands)\n";
        return static_cast<int>(lcart::cli::ExitStatus::usage);
    }
    return static_cast<int>(lcart::cli::run(*command, std::cout, std::cerr));
}
