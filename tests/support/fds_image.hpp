#pragma once

#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

#include "core/fds/fds.hpp"

namespace mapperbus::tests {

/// Standard dump with one 256-byte CPU file on each side, no BIOS or game data.
inline std::vector<core::Byte> make_fds_image(int sides = 1, bool headered = false) {
    const std::size_t prefix = headered ? 16 : 0;
    std::vector<core::Byte> image(prefix + static_cast<std::size_t>(sides) * core::Fds::kSideSize);
    if (headered) {
        const std::array<core::Byte, 5> header{'F', 'D', 'S', 0x1A, static_cast<core::Byte>(sides)};
        std::ranges::copy(header, image.begin());
    }
    for (int side = 0; side < sides; ++side) {
        const auto offset = prefix + static_cast<std::size_t>(side) * core::Fds::kSideSize;
        auto* data = image.data() + offset;
        data[0] = 1;
        constexpr std::string_view signature = "*NINTENDO-HVC*";
        std::ranges::copy(signature, data + 1);
        data[21] = static_cast<core::Byte>(side % 2);
        data[22] = static_cast<core::Byte>(side / 2);
        data[56] = 2;
        data[57] = 1;
        data[58] = 3;
        data[58 + 12] = 0x60; // destination $6000
        data[58 + 14] = 1;    // length $0100
        data[74] = 4;
        for (std::size_t index = 0; index < 256; ++index)
            data[75 + index] = static_cast<core::Byte>(index + static_cast<std::size_t>(side) * 7);
    }
    return image;
}

/// 6502 BIOS fixture: enables disk I/O, starts reads, copies block-1's first
/// byte to $6000, then loops. All vectors point at $E000.
inline std::vector<core::Byte> make_fds_bios() {
    std::vector<core::Byte> bios(8192, 0xEA);
    const core::Byte program[] = {
        0x78, 0xA9, 0x01, 0x8D, 0x23, 0x40, 0xA9, 0x65, 0x8D, 0x25, 0x40, 0xAD, 0x30,
        0x40, 0x10, 0xFB, 0xAD, 0x31, 0x40, 0x8D, 0x00, 0x60, 0x4C, 0x16, 0xE0,
    };
    std::ranges::copy(program, bios.begin());
    for (std::size_t vector = 8192 - 6; vector < 8192; vector += 2) {
        bios[vector] = 0;
        bios[vector + 1] = 0xE0;
    }
    return bios;
}

} // namespace mapperbus::tests
