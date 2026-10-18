// Verifies: adr0028-run-id-v1 (the SHA-256 of `common` that run IDs and
// MANIFEST.json use: the examples of FIPS 180-4, lengths around the block
// boundary, and the file hashes LLVM's implementation recorded in the golden
// facts export).
#include "common/sha256.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace {

namespace fs = std::filesystem;
using lcart::common::sha256_hex;

std::string read_file(const fs::path& path) {
    // Not istreambuf_iterator: GCC 14 at -O3 reports -Wnull-dereference in it.
    const std::ifstream in{path, std::ios::binary};
    std::ostringstream text;
    text << in.rdbuf();
    return std::move(text).str();
}

TEST(Sha256, ExamplesOfTheStandard) {
    EXPECT_EQ(sha256_hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256_hex("abc"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    EXPECT_EQ(sha256_hex("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                         "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"),
              "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
    constexpr std::size_t million = 1'000'000;
    EXPECT_EQ(sha256_hex(std::string(million, 'a')),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// The padding needs a second block from 56 bytes per block on.
TEST(Sha256, LengthsAroundTheBlockBoundary) {
    const std::array<std::pair<std::size_t, std::string_view>, 7> expected{{
        {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
        {119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"},
        {120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"},
    }};
    for (const auto& [length, digest] : expected) {
        EXPECT_EQ(sha256_hex(std::string(length, 'a')), digest) << length;
    }
}

TEST(Sha256, EveryByteValue) {
    std::string bytes;
    constexpr int byte_values = 256;
    for (int value = 0; value < byte_values; ++value) {
        bytes += static_cast<char>(value);
    }
    EXPECT_EQ(sha256_hex(bytes), "40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880");
}

// The analysis hashes files with LLVM's SHA-256; ours must agree.
TEST(Sha256, AgreesWithTheHashesOfTheGoldenExport) {
    std::istringstream golden{read_file(fs::path{LCART_GOLDEN_DIR} / "two_modules.facts.jsonl")};
    int compared = 0;
    for (std::string line; std::getline(golden, line);) {
        const auto record = nlohmann::json::parse(line);
        if (record["record"] != "file" || record["sha256"].is_null()) {
            continue;
        }
        const fs::path file =
            fs::path{LCART_FIXTURES_DIR} / "two_modules" / record["path"].get<std::string>();
        EXPECT_EQ(sha256_hex(read_file(file)), record["sha256"].get<std::string>()) << file;
        ++compared;
    }
    EXPECT_EQ(compared, 5);
}

} // namespace
