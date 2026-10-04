// libFuzzer target for the lcart.toml parser and its path globs (B-11,
// NFR-010). Built only with -DLCART_BUILD_FUZZERS=ON and Clang.
#include "config/project_config.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string text(data, data + size);
    auto config = lcart::config::parse_project_config(text, "fuzz.toml");
    if (config) {
        // Every accepted glob must also be safe to match.
        for (const lcart::config::PathGlob& glob : config->exclude) {
            static_cast<void>(glob.matches(text));
        }
        for (const lcart::config::ModuleRule& rule : config->modules) {
            for (const lcart::config::PathGlob& glob : rule.paths) {
                static_cast<void>(glob.matches("src/modbus-rtu.c"));
                static_cast<void>(glob.matches(text));
            }
        }
    }
    return 0;
}
