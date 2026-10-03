// libFuzzer target for the claim parser: LLM output is untrusted input
// (docs/security.md). Built only with -DLCART_BUILD_FUZZERS=ON and Clang.
#include <cstddef>
#include <cstdint>

// NOLINTNEXTLINE(readability-identifier-naming): name required by libFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // TODO(M2): feed the bytes to lcart::claims::parse_claims() once it exists.
    static_cast<void>(data);
    static_cast<void>(size);
    return 0;
}
