#pragma once

#include <algorithm>
#include <array>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::tests {

inline std::vector<core::Byte> make_dcs_base_rom() {
    std::vector<core::Byte> image(16U + 0x20000U + 0x2000U, 0U);
    image[0U] = 'N';
    image[1U] = 'E';
    image[2U] = 'S';
    image[3U] = 0x1AU;
    image[4U] = 8U;
    image[5U] = 1U;
    image[6U] = 0x40U;
    image[7U] = 0x48U;
    image[8U] = 0x10U;
    image[10U] = 7U;
    for (std::size_t i = 0U; i < 0x20000U; ++i)
        image[16U + i] = static_cast<core::Byte>(i / 0x4000U);
    // Execute from the fixed bank: select external ROM, strobe, then loop.
    const std::array<core::Byte, 11U> program = {
        0xA9U, 0x00U, 0x8DU, 0x00U, 0xF0U, 0x8DU, 0x00U, 0x60U, 0x4CU, 0x08U, 0xE0U};
    std::copy(program.begin(), program.end(), image.begin() + 16U + 0x1E000U);
    for (std::size_t i = 0x1FFFAU; i < 0x20000U; i += 2U) {
        image[16U + i] = 0U;
        image[16U + i + 1U] = 0xE0U;
    }
    return image;
}

} // namespace mapperbus::tests
