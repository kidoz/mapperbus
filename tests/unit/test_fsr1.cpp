#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

#include "platform/video/fsr1.hpp"

namespace {
std::vector<std::uint32_t> make_pattern(int width, int height, std::uint32_t seed) {
    std::vector<std::uint32_t> source(width * height);
    for (auto& pixel : source) {
        seed = seed * 1664525 + 1013904223;
        pixel = 0xFF000000U | (seed & 0x00FFFFFFU);
    }
    return source;
}

bool matches_with_rounding(std::uint32_t pixel, std::uint32_t color) {
    if ((pixel >> 24) != (color >> 24))
        return false;
    for (int shift = 0; shift <= 16; shift += 8) {
        const int actual = static_cast<int>((pixel >> shift) & 0xFF);
        const int expected = static_cast<int>((color >> shift) & 0xFF);
        if (actual < expected - 1 || actual > expected + 1)
            return false;
    }
    return true;
}
} // namespace

TEST_CASE("CPU FSR1 preserves solid colors within integer rounding", "[video][fsr1]") {
    constexpr int width = 7;
    constexpr int height = 5;
    constexpr std::array colors = {0xFFFF0000U, 0xFF00FF00U, 0xFF0000FFU};

    for (int factor : {2, 3, 4, 5, 6}) {
        mapperbus::platform::Fsr1Upscaler upscaler(factor);
        std::vector<std::uint32_t> source(width * height);
        std::vector<std::uint32_t> target(source.size() * factor * factor);
        for (const auto color : colors) {
            std::fill(source.begin(), source.end(), color);
            upscaler.scale(source, width, height, target);
            REQUIRE(std::all_of(target.begin(), target.end(), [color](std::uint32_t pixel) {
                return matches_with_rounding(pixel, color);
            }));
        }
    }
}

TEST_CASE("CPU FSR1 reused workers do not retain stale frame data", "[video][fsr1]") {
    constexpr std::array dimensions = {
        std::array{3, 2}, std::array{7, 5}, std::array{16, 9}, std::array{3, 2}};
    mapperbus::platform::Fsr1Upscaler upscaler(3);

    for (const auto [width, height] : dimensions) {
        auto source_a = make_pattern(width, height, 0x12345678);
        auto source_b = make_pattern(width, height, 0x87654321);
        std::vector<std::uint32_t> expected(source_a.size() * 9);
        std::vector<std::uint32_t> actual(expected.size());
        std::vector<std::uint32_t> other(expected.size());

        upscaler.scale(source_a, width, height, expected);
        for (int iteration = 0; iteration < 40; ++iteration) {
            upscaler.scale(source_b, width, height, other);
            upscaler.scale(source_a, width, height, actual);
            INFO("dimensions=" << width << 'x' << height << ", iteration=" << iteration);
            REQUIRE(actual == expected);
            REQUIRE(std::all_of(actual.begin(), actual.end(), [](std::uint32_t pixel) {
                return (pixel >> 24) == 0xFF;
            }));
        }
    }
}
