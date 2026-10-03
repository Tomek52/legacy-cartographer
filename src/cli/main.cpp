#include "common/version.hpp"

#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>

namespace {

constexpr std::string_view usage = R"(usage: lcart <command> [options]

Commands (planned, see docs/plan/roadmap.md):
  analyze    build the fact database from compile_commands.json   (M1)
  query      query facts: callers, callees, includes, modules     (M1)
  map        write the module map (Markdown + Mermaid), no LLM    (M1)
  document   generate documentation and claims with an LLM        (M2)
  verify     verify claims against the fact database             (M2)
  testgen    generate a characterization test skeleton           (M3)

Options:
  --help       show this message
  --version    show the tool version
)";

} // namespace

int main(int argc, char** argv) {
    const std::span args(argv, static_cast<std::size_t>(argc));
    if (args.size() == 2 && std::string_view{args[1]} == "--version") {
        std::cout << "lcart " << lcart::common::version << '\n';
        return EXIT_SUCCESS;
    }
    if (args.size() == 2 && std::string_view{args[1]} == "--help") {
        std::cout << usage;
        return EXIT_SUCCESS;
    }
    // TODO(B-12): command dispatch (analyze, query) once the fact store exists.
    std::cerr << usage;
    return 2;
}
