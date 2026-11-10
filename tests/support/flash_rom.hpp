#pragma once

#include <algorithm>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::tests {
/// Original program copies itself to CPU RAM before flashing physical byte 8100h.
inline std::vector<core::Byte> flash_rom(uint16_t id, bool legacy = false) {
    using core::Byte;
    std::vector<Byte> image(16U + 524288U, 0xFFU);
    std::fill_n(image.begin(), 16U, 0U);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = 32U;
    image[6] = static_cast<Byte>(((id & 15U) << 4U) | (id == 30U ? 2U : 0U));
    image[7] = static_cast<Byte>((id & 0xF0U) | (legacy ? 0U : 8U));
    if (!legacy)
        image[11] = 9U; // 32 KiB volatile PPU RAM, no PRG NVRAM declaration.
    std::vector<Byte> code;
    const auto store = [&code](core::Address addr, Byte value) {
        code.insert(code.end(),
                    {0xA9U, value, 0x8DU, static_cast<Byte>(addr), static_cast<Byte>(addr >> 8U)});
    };
    if (id == 30U) {
        store(0xC000U, 1U);
        store(0x9555U, 0xAAU);
        store(0xC000U, 0U);
        store(0xAAAAU, 0x55U);
        store(0xC000U, 1U);
        store(0x9555U, 0xA0U);
        store(0xC000U, 2U);
    } else {
        store(0x5000U, 0U);
        store(0xD555U, 0xAAU);
        store(0xAAAAU, 0x55U);
        store(0xD555U, 0xA0U);
        store(0x5000U, 1U);
    }
    store(0x8100U, 0x5AU);
    // Poll until the byte is committed, then expose it in CPU RAM $10.
    code.insert(code.end(), {0xADU, 0U, 0x81U, 0xC9U, 0x5AU, 0xD0U, 0xF9U, 0x85U, 0x10U});
    const auto loop = static_cast<core::Address>(0x200U + code.size());
    code.insert(code.end(), {0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    const std::vector<Byte> boot = {0x78U,
                                    0xA2U,
                                    0U,
                                    0xBDU,
                                    0U,
                                    0xE0U,
                                    0x9DU,
                                    0U,
                                    2U,
                                    0xE8U,
                                    0xE0U,
                                    static_cast<Byte>(code.size()),
                                    0xD0U,
                                    0xF5U,
                                    0x4CU,
                                    0U,
                                    2U};
    for (std::size_t bank = 0U; bank < 524288U; bank += 32768U) {
        std::ranges::copy(boot, image.begin() + static_cast<std::ptrdiff_t>(16U + bank + 0x4000U));
        std::ranges::copy(code, image.begin() + static_cast<std::ptrdiff_t>(16U + bank + 0x6000U));
        for (unsigned vector = 0x7FFAU; vector <= 0x7FFEU; vector += 2U) {
            image[16U + bank + vector] = 0U;
            image[16U + bank + vector + 1U] = 0xC0U;
        }
    }
    return image;
}
} // namespace mapperbus::tests
