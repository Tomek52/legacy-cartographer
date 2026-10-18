#include "common/sha256.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace lcart::common {

namespace {

// FIPS 180-4, section 4.2.2: the first 32 bits of the fractional parts of
// the cube roots of the first 64 primes.
constexpr std::array<std::uint32_t, 64> round_constants{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};

// Section 5.3.3: the first 32 bits of the fractional parts of the square
// roots of the first 8 primes.
constexpr std::array<std::uint32_t, 8> initial_hash{0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                                    0xa54ff53a, 0x510e527f, 0x9b05688c,
                                                    0x1f83d9ab, 0x5be0cd19};

constexpr std::size_t block_size = 64;
/// The message length in bits, appended big-endian at the end of the
/// last block.
constexpr std::size_t length_size = 8;
constexpr unsigned char end_of_message = 0x80;
constexpr std::uint32_t bits_per_byte = 8;

using Block = std::array<unsigned char, block_size>;

// Section 6.2.2.
void compress(std::array<std::uint32_t, 8>& hash, const Block& block) {
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t t = 0; t < 16; ++t) {
        schedule.at(t) = (std::uint32_t{block.at(4 * t)} << 24U) |
                         (std::uint32_t{block.at((4 * t) + 1)} << 16U) |
                         (std::uint32_t{block.at((4 * t) + 2)} << 8U) |
                         std::uint32_t{block.at((4 * t) + 3)};
    }
    for (std::size_t t = 16; t < schedule.size(); ++t) {
        const std::uint32_t w15 = schedule.at(t - 15);
        const std::uint32_t w2 = schedule.at(t - 2);
        const std::uint32_t s0 = std::rotr(w15, 7) ^ std::rotr(w15, 18) ^ (w15 >> 3U);
        const std::uint32_t s1 = std::rotr(w2, 17) ^ std::rotr(w2, 19) ^ (w2 >> 10U);
        schedule.at(t) = schedule.at(t - 16) + s0 + schedule.at(t - 7) + s1;
    }
    auto [a, b, c, d, e, f, g, h] = hash;
    for (std::size_t t = 0; t < schedule.size(); ++t) {
        const std::uint32_t sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const std::uint32_t choice = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + sum1 + choice + round_constants.at(t) + schedule.at(t);
        const std::uint32_t sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    const std::array<std::uint32_t, 8> working{a, b, c, d, e, f, g, h};
    for (std::size_t i = 0; i < hash.size(); ++i) {
        hash.at(i) += working.at(i);
    }
}

} // namespace

std::string sha256_hex(std::string_view data) {
    std::array<std::uint32_t, 8> hash = initial_hash;
    const auto to_byte = [](char character) { return static_cast<unsigned char>(character); };
    Block block{};
    std::string_view rest = data;
    for (; rest.size() >= block_size; rest.remove_prefix(block_size)) {
        std::ranges::transform(rest.substr(0, block_size), block.begin(), to_byte);
        compress(hash, block);
    }

    // Section 5.1.1: the rest, a 1 bit, zeros, and the length in bits, in
    // one block or, if the length does not fit after the rest, two.
    std::array<unsigned char, 2 * block_size> tail{};
    std::ranges::transform(rest, tail.begin(), to_byte);
    tail.at(rest.size()) = end_of_message;
    const std::size_t tail_size =
        rest.size() + 1 + length_size <= block_size ? block_size : 2 * block_size;
    const std::uint64_t bit_length = std::uint64_t{data.size()} * bits_per_byte;
    for (std::size_t i = 0; i < length_size; ++i) {
        tail.at(tail_size - 1 - i) = static_cast<unsigned char>(bit_length >> (bits_per_byte * i));
    }
    for (std::size_t at = 0; at < tail_size; at += block_size) {
        std::ranges::copy_n(tail.begin() + static_cast<std::ptrdiff_t>(at), block_size,
                            block.begin());
        compress(hash, block);
    }

    static constexpr std::string_view hex_digits = "0123456789abcdef";
    std::string hex;
    hex.reserve(hash.size() * 8);
    for (const std::uint32_t word : hash) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            hex += hex_digits[(word >> static_cast<unsigned>(shift)) & 0xFU];
        }
    }
    return hex;
}

} // namespace lcart::common
