#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace mapperbus::tests::xbrz {

using Pixels = std::vector<std::uint32_t>;

inline Pixels rotate_clockwise(const Pixels& source, int width) {
    Pixels rotated(source.size());
    for (int y = 0; y < width; ++y) {
        for (int x = 0; x < width; ++x) {
            rotated[x * width + width - 1 - y] = source[y * width + x];
        }
    }
    return rotated;
}

inline constexpr int kAtlasWidth = 120;

inline Pixels neighborhood_atlas(bool multicolor) {
    // 512 disjoint 3x3 neighborhoods in 5x5 tiles. The fixed palette includes
    // near-tolerance greys, unequal gradients, and distinct RGB channels.
    constexpr std::array<std::uint32_t, 12> kPalette = {0xFF000000U,
                                                        0xFFFFFFFFU,
                                                        0xFF080808U,
                                                        0xFF0A0A0AU,
                                                        0xFF0B0B0BU,
                                                        0xFF282828U,
                                                        0xFF646464U,
                                                        0xFFB4B4B4U,
                                                        0xFF80A0C0U,
                                                        0xFFE02040U,
                                                        0xFF20C060U,
                                                        0xFF6040E0U};
    Pixels source(kAtlasWidth * kAtlasWidth, 0xFF000000U);
    std::uint32_t state = 0x5842525AU;
    for (unsigned sample = 0; sample < 512; ++sample) {
        const int origin_x = static_cast<int>(sample % 24) * 5 + 1;
        const int origin_y = static_cast<int>(sample / 24) * 5 + 1;
        for (int y = 0; y < 3; ++y) {
            for (int x = 0; x < 3; ++x) {
                state = state * 1664525U + 1013904223U;
                const auto color = multicolor ? kPalette[(state >> 16) % kPalette.size()]
                                              : kPalette[(sample >> (y * 3 + x)) & 1U];
                source[(origin_y + y) * kAtlasWidth + origin_x + x] = color;
            }
        }
    }
    return source;
}

struct GoldenPatch {
    std::string_view name;
    Pixels source;
    int x;
    int y;
    std::array<std::string_view, 5> cpu;
    std::array<std::string_view, 5> gpu;
};

// Hand-calculated center blocks for factors 2 through 6. C has channels
// divisible by eight, so CPU truncation and GPU UNORM rounding agree here.
// Q/T/E/H/A/D/V are respectively 2/8, 3/8, 1/8, 4/8, 6/8, 5/8, 7/8 of C.
// CPU and GPU retain their different edge footprints at scales above 2.
inline std::uint32_t golden_color(char symbol) {
    switch (symbol) {
    case 'C':
        return 0xFF80A0C0U;
    case 'Q':
        return 0xFF202830U;
    case 'T':
        return 0xFF303C48U;
    case 'E':
        return 0xFF101418U;
    case 'H':
        return 0xFF405060U;
    case 'A':
        return 0xFF607890U;
    case 'D':
        return 0xFF506478U;
    case 'V':
        return 0xFF708CA8U;
    default:
        return 0xFF000000U;
    }
}

inline std::array<GoldenPatch, 4> golden_patches() {
    constexpr auto kO = 0xFF000000U;
    constexpr auto kC = 0xFF80A0C0U;
    return {{
        {"isolated colored pixel",
         {kO, kO, kO, kO, kC, kO, kO, kO, kO},
         1,
         1,
         {"AA"
          "AA",
          "AAA"
          "ACA"
          "AAA",
          "AHHA"
          "HCCH"
          "HCCH"
          "AHHA",
          "ADADA"
          "DCCCD"
          "ACCCA"
          "DCCCD"
          "ADADA",
          "ADVVDA"
          "DCCCCD"
          "VCCCCV"
          "VCCCCV"
          "DCCCCD"
          "ADVVDA"},
         {"AA"
          "AA",
          "AAA"
          "ACA"
          "AAA",
          "AVVA"
          "VCCV"
          "VCCV"
          "AVVA",
          "AVAVA"
          "VCCCV"
          "ACCCA"
          "VCCCV"
          "AVAVA",
          "AVVVVA"
          "VCCCCV"
          "VCCCCV"
          "VCCCCV"
          "VCCCCV"
          "AVVVVA"}},
        {"one-pixel diagonal line",
         {kC, kO, kO, kO, kC, kO, kO, kO, kC},
         1,
         1,
         {"CA"
          "AC",
          "CCA"
          "CCC"
          "ACC",
          "CVDA"
          "VCCD"
          "DCCV"
          "ADVC",
          "CCVDA"
          "CCCCD"
          "VCCCV"
          "DCCCC"
          "ADVCC",
          "CCCVDA"
          "CCCCCD"
          "CCCCCV"
          "VCCCCC"
          "DCCCCC"
          "ADVCCC"},
         {"CA"
          "AC",
          "CCA"
          "CCC"
          "ACC",
          "CCCA"
          "CCCC"
          "CCCC"
          "ACCC",
          "CCCCA"
          "CCCCC"
          "CCCCC"
          "CCCCC"
          "ACCCC",
          "CCCCCA"
          "CCCCCC"
          "CCCCCC"
          "CCCCCC"
          "CCCCCC"
          "ACCCCC"}},
        {"single colored corner",
         {kC, kO, kO, kO, kO, kO, kO, kO, kO},
         1,
         1,
         {"QO"
          "OO",
          "QEO"
          "EOO"
          "OOO",
          "QTEO"
          "TOOO"
          "EOOO"
          "OOOO",
          "QTEOO"
          "TOOOO"
          "EOOOO"
          "OOOOO"
          "OOOOO",
          "QTEOOO"
          "TOOOOO"
          "EOOOOO"
          "OOOOOO"
          "OOOOOO"
          "OOOOOO"},
         {"QO"
          "OO",
          "QEO"
          "EOO"
          "OOO",
          "QEOO"
          "EOOO"
          "OOOO"
          "OOOO",
          "QEEOO"
          "EOOOO"
          "EOOOO"
          "OOOOO"
          "OOOOO",
          "QEEOOO"
          "EOOOOO"
          "EOOOOO"
          "OOOOOO"
          "OOOOOO"
          "OOOOOO"}},
        {"clamped texture corner",
         {kO, kC, kC, kC, kC, kC, kC, kC, kC},
         0,
         0,
         {"OQ"
          "QQ",
          "OOQ"
          "OOO"
          "QOQ",
          "OETQ"
          "EOOH"
          "TOOH"
          "QHHQ",
          "OOETQ"
          "OOOOT"
          "EOOOQ"
          "TOOOT"
          "QTQTQ",
          "OOOETQ"
          "OOOOOT"
          "OOOOOE"
          "EOOOOE"
          "TOOOOT"
          "QTEETQ"},
         {"OQ"
          "QQ",
          "OOQ"
          "OOO"
          "QOQ",
          "OOOQ"
          "OOOO"
          "OOOO"
          "QOOQ",
          "OOOOQ"
          "OOOOO"
          "OOOOO"
          "OOOOO"
          "QOOOQ",
          "OOOOOQ"
          "OOOOOO"
          "OOOOOO"
          "OOOOOO"
          "OOOOOO"
          "QOOOOQ"}},
    }};
}

} // namespace mapperbus::tests::xbrz
