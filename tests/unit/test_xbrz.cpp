#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "../support/xbrz_cases.hpp"
#include "platform/video/xbrz.hpp"

namespace {
using mapperbus::tests::xbrz::Pixels;
using mapperbus::tests::xbrz::rotate_clockwise;

Pixels scale(const Pixels& source, int width, int factor) {
    Pixels output(source.size() * factor * factor);
    mapperbus::platform::XbrzUpscaler upscaler(factor);
    upscaler.scale(source, width, width, output);
    return output;
}

TEST_CASE("xBRZ color tolerance uses distance rather than squared distance", "[xbrz]") {
    // The redmean distance between these greys is 24, below the default 30.
    // Squaring the distance without changing the tolerance would blend them.
    constexpr int kWidth = 5;
    Pixels source(static_cast<std::size_t>(kWidth) * kWidth, 0xFF000000U);
    source[1 * kWidth + 1] = 0xFF080808U;
    for (int factor : {2, 3, 4, 5, 6}) {
        CAPTURE(factor);
        const auto output = scale(source, kWidth, factor);
        for (int y = 0; y < kWidth * factor; ++y) {
            for (int x = 0; x < kWidth * factor; ++x) {
                REQUIRE(output[y * kWidth * factor + x] ==
                        source[(y / factor) * kWidth + x / factor]);
            }
        }
    }
}

TEST_CASE("xBRZ treats rotated binary neighborhoods consistently at every scale", "[xbrz]") {
    constexpr int kWidth = 5;
    // Exhaust every 3x3 binary neighborhood, with a one-pixel border so the
    // center block never depends on texture-edge clamping.
    for (std::uint32_t mask = 0; mask < 512; ++mask) {
        CAPTURE(mask);
        Pixels source(static_cast<std::size_t>(kWidth) * kWidth, 0xFF000000U);
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 3; ++x) {
                if ((mask & (1U << static_cast<unsigned>(y * 3 + x))) != 0U) {
                    source[(y + 1) * kWidth + x + 1] = 0xFFFFFFFFU;
                }
            }
        }
        for (int factor : {2, 3, 4, 5, 6}) {
            CAPTURE(factor);
            const auto expected = rotate_clockwise(scale(source, kWidth, factor), kWidth * factor);
            const auto actual = scale(rotate_clockwise(source, kWidth), kWidth, factor);
            for (int y = 2 * factor; y < 3 * factor; ++y) {
                for (int x = 2 * factor; x < 3 * factor; ++x) {
                    CAPTURE(x, y);
                    REQUIRE(actual[y * kWidth * factor + x] == expected[y * kWidth * factor + x]);
                }
            }
        }
    }
}

TEST_CASE("xBRZ rotates deterministic multicolor neighborhoods at every scale", "[xbrz]") {
    using namespace mapperbus::tests::xbrz;
    for (int factor : {2, 3, 4, 5, 6}) {
        auto source = neighborhood_atlas(true);
        auto expected = scale(source, kAtlasWidth, factor);
        for (int turn = 1; turn <= 3; ++turn) {
            source = rotate_clockwise(source, kAtlasWidth);
            expected = rotate_clockwise(expected, kAtlasWidth * factor);
            const auto actual = scale(source, kAtlasWidth, factor);
            const auto mismatch = std::mismatch(actual.begin(), actual.end(), expected.begin());
            const auto offset = mismatch.first - actual.begin();
            CAPTURE(factor, turn, offset);
            REQUIRE(mismatch.first == actual.end());
        }
    }
}

TEST_CASE("xBRZ matches hand-calculated corner and line blocks", "[xbrz]") {
    using namespace mapperbus::tests::xbrz;
    for (const auto& patch : golden_patches()) {
        for (int factor : {2, 3, 4, 5, 6}) {
            auto source = patch.source;
            int sx = patch.x;
            int sy = patch.y;
            Pixels expected;
            for (char symbol : patch.cpu[factor - 2]) {
                expected.push_back(golden_color(symbol));
            }
            REQUIRE(expected.size() == static_cast<std::size_t>(factor * factor));
            for (int turn = 0; turn < 4; ++turn) {
                const auto output = scale(source, 3, factor);
                for (int y = 0; y < factor; ++y) {
                    for (int x = 0; x < factor; ++x) {
                        CAPTURE(patch.name, factor, turn, x, y);
                        REQUIRE(output[(sy * factor + y) * (3 * factor) + sx * factor + x] ==
                                expected[y * factor + x]);
                    }
                }
                source = rotate_clockwise(source, 3);
                expected = rotate_clockwise(expected, factor);
                const int next_x = 2 - sy;
                sy = sx;
                sx = next_x;
            }
        }
    }
}

TEST_CASE("xBRZ compares sums and ratios of linear color distances", "[xbrz]") {
    constexpr int kWidth = 5;
    Pixels source(static_cast<std::size_t>(kWidth) * kWidth, 0xFF000000U);
    source[1 * kWidth + 1] = 0xFF646464U;
    source[1 * kWidth + 2] = 0xFF646464U;
    source[1 * kWidth + 3] = 0xFF282828U;
    source[2 * kWidth + 1] = 0xFF282828U;
    source[2 * kWidth + 3] = 0xFF646464U;
    // Diagonal gradient 300 is less than orthogonal gradient 180 + 180.
    // Squared gradients reverse the decision: 90000 > 32400 + 32400.
    REQUIRE(scale(source, kWidth, 2)[4 * kWidth * 2 + 4] == 0xFF000000U);

    source.assign(static_cast<std::size_t>(kWidth) * kWidth, 0xFF000000U);
    source[1 * kWidth + 1] = 0xFF646464U;
    source[1 * kWidth + 2] = 0xFFB4B4B4U;
    source[2 * kWidth + 1] = 0xFFB4B4B4U;
    // Orthogonal distance 540 < 2.2 * diagonal distance 300 permits blending.
    // Squared distances would reject it: 291600 > 2.2 * 90000.
    REQUIRE(scale(source, kWidth, 2)[4 * kWidth * 2 + 4] == 0xFF191919U);
}

TEST_CASE("xBRZ borders match an explicitly edge-padded image", "[xbrz]") {
    for (const auto [width, height] : {std::pair{1, 1}, {1, 5}, {5, 1}, {7, 5}}) {
        CAPTURE(width, height);
        Pixels source(static_cast<std::size_t>(width) * height);
        for (int i = 0; i < width * height; ++i) {
            source[i] = 0xFF000000U | (static_cast<std::uint32_t>(i * 0x7193B5U) & 0xFFFFFFU);
        }
        const int padded_width = width + 2;
        const int padded_height = height + 2;
        Pixels padded(static_cast<std::size_t>(padded_width) * padded_height);
        for (int y = 0; y < padded_height; ++y) {
            for (int x = 0; x < padded_width; ++x) {
                padded[y * padded_width + x] = source[std::clamp(y - 1, 0, height - 1) * width +
                                                      std::clamp(x - 1, 0, width - 1)];
            }
        }
        for (int factor : {2, 3, 4, 5, 6}) {
            CAPTURE(factor);
            Pixels actual(source.size() * factor * factor);
            Pixels expected(padded.size() * factor * factor);
            mapperbus::platform::XbrzUpscaler upscaler(factor);
            upscaler.scale(source, width, height, actual);
            upscaler.scale(padded, padded_width, padded_height, expected);
            for (int y = 0; y < height * factor; ++y) {
                for (int x = 0; x < width * factor; ++x) {
                    REQUIRE(actual[y * width * factor + x] ==
                            expected[(y + factor) * padded_width * factor + x + factor]);
                }
            }
        }
    }
}

TEST_CASE("xBRZ rejects unsupported standalone scale factors without writing", "[xbrz]") {
    const Pixels source(1, 0xFF80A0C0U);
    const Pixels untouched(144, 0x12345678U);
    for (int factor : {-1, 0, 1, 7, 12}) {
        CAPTURE(factor);
        Pixels output = untouched;
        mapperbus::platform::xbrz_scale(factor, source, 1, 1, output);
        REQUIRE(output == untouched);
    }
}

} // namespace
