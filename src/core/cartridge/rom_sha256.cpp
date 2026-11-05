#include "core/cartridge/rom_sha256.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <string_view>

namespace mapperbus::core {
namespace {
// SHA-256 compression and padding, FIPS 180-4 sections 5 and 6.2.
constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

void compress(std::span<const Byte, 64> block, std::array<std::uint32_t, 8>& state) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i) {
        const auto j = i * 4;
        words[i] = (std::uint32_t{block[j]} << 24U) | (std::uint32_t{block[j + 1]} << 16U) |
                   (std::uint32_t{block[j + 2]} << 8U) | std::uint32_t{block[j + 3]};
    }
    for (std::size_t i = 16; i < words.size(); ++i) {
        const auto x = words[i - 15];
        const auto y = words[i - 2];
        const auto s0 = std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3U);
        const auto s1 = std::rotr(y, 17) ^ std::rotr(y, 19) ^ (y >> 10U);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    auto [a, b, c, d, e, f, g, h] = state;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const auto s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const auto choose = (e & f) ^ (~e & g);
        const auto t1 = h + s1 + choose + kRoundConstants[i] + words[i];
        const auto s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto t2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    const std::array<std::uint32_t, 8> next = {a, b, c, d, e, f, g, h};
    for (std::size_t i = 0; i < state.size(); ++i) {
        state[i] += next[i];
    }
}
} // namespace

Sha256Digest rom_sha256(std::span<const Byte> data) {
    std::array<std::uint32_t, 8> state = {0x6a09e667,
                                          0xbb67ae85,
                                          0x3c6ef372,
                                          0xa54ff53a,
                                          0x510e527f,
                                          0x9b05688c,
                                          0x1f83d9ab,
                                          0x5be0cd19};
    std::size_t position = 0;
    while (data.size() - position >= 64) {
        compress(std::span<const Byte, 64>(data.data() + position, 64), state);
        position += 64;
    }
    std::array<Byte, 128> tail{};
    const auto remaining = data.size() - position;
    std::copy(data.begin() + static_cast<std::ptrdiff_t>(position), data.end(), tail.begin());
    tail[remaining] = 0x80;
    const std::size_t padded_size = remaining < 56 ? 64 : 128;
    const auto bit_length = static_cast<std::uint64_t>(data.size()) * 8U;
    for (std::size_t i = 0; i < 8; ++i) {
        tail[padded_size - 1 - i] = static_cast<Byte>(bit_length >> (i * 8U));
    }
    compress(std::span<const Byte, 64>(tail.data(), 64), state);
    if (padded_size == 128) {
        compress(std::span<const Byte, 64>(tail.data() + 64, 64), state);
    }
    Sha256Digest digest{};
    for (std::size_t i = 0; i < state.size(); ++i) {
        for (std::size_t j = 0; j < 4; ++j) {
            digest[i * 4 + j] = static_cast<Byte>(state[i] >> ((3 - j) * 8U));
        }
    }
    return digest;
}

std::string sha256_hex(const Sha256Digest& digest) {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string result;
    result.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        result += kDigits[byte / 16];
        result += kDigits[byte % 16];
    }
    return result;
}

} // namespace mapperbus::core
