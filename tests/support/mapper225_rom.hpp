#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::tests {
// Original firmware runs from console RAM so address-latched writes can replace
// both PRG windows. All banks contain the same startup trampoline and vectors.
inline std::vector<core::Byte> make_mapper225_rom(bool legacy = false, bool vertical = true) {
    using core::Address;
    using core::Byte;
    std::vector<Byte> image(16U + 2097152U + 1048576U);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = 128U;
    image[5] = 128U;
    image[6] = static_cast<Byte>(0x10U | (vertical ? 1U : 0U));
    image[7] = legacy ? 0xE0U : 0xE8U;
    std::vector<Byte> code;
    const auto store = [&code](Address addr, Byte value) {
        code.insert(code.end(),
                    {0xA9U, value, 0x8DU, static_cast<Byte>(addr), static_cast<Byte>(addr >> 8U)});
    };
    const auto expect = [&code](Address addr, Byte value) {
        code.insert(code.end(),
                    {0xADU,
                     static_cast<Byte>(addr),
                     static_cast<Byte>(addr >> 8U),
                     0xC9U,
                     value,
                     0xD0U,
                     0xFEU});
    };
    const auto paddr = [&store](Address addr) {
        store(0x2006U, static_cast<Byte>(addr >> 8U));
        store(0x2006U, static_cast<Byte>(addr));
    };
    const auto pread = [&paddr, &code, &expect](Address addr, Byte value) {
        paddr(addr);
        code.insert(code.end(), {0xADU, 7U, 0x20U});
        expect(0x2007U, value);
    };
    store(0x2000U, 0U);
    store(0x2001U, 0U);
    store(0x12U, 0U);
    for (Byte bank : {0U, 63U, 64U, 127U})
        for (bool narrow : {false, true})
            for (bool horizontal : {false, true}) {
                const unsigned chr = (bank * 7U + 3U) & 63U;
                const auto latch = static_cast<Address>(
                    0x8000U | ((bank & 64U) << 8U) | (horizontal ? 0x2000U : 0U) |
                    (narrow ? 0x1000U : 0U) | ((bank & 63U) << 6U) | chr);
                store(latch, static_cast<Byte>(bank ^ 0xA5U));
                expect(0x9A00U, narrow ? bank : bank & 126U);
                expect(0xDA00U, narrow ? bank : bank | 1U);
                paddr(0U);
                store(0x2007U, 0xFFU);
                pread(0U, static_cast<Byte>((bank & 64U) | chr));
            }
    for (bool horizontal : {false, true}) {
        store(horizontal ? 0xA000U : 0x8000U, 0xFFU);
        paddr(0x2000U);
        store(0x2007U, 0x19U);
        paddr(0x2400U);
        store(0x2007U, 0x27U);
        pread(0x2000U, horizontal ? 0x27U : 0x19U);
        paddr(0x2800U);
        store(0x2007U, 0x35U);
        pread(0x2000U, horizontal ? 0x27U : 0x35U);
    }
    store(0x5800U, 0xF9U);
    code.insert(code.end(), {0xADU, 0xFCU, 0x5FU, 0x29U, 0x0FU, 0xC9U, 9U, 0xD0U, 0xFEU});
    store(0x12U, 0xD5U);
    const auto loop = static_cast<Address>(0x200U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    assert(code.size() <= 0x600U);
    std::vector<Byte> boot{0x78U, 0xD8U, 0xA2U, 0xFFU, 0x9AU};
    for (unsigned page = 0U; page < (code.size() + 255U) / 256U; ++page) {
        boot.insert(boot.end(),
                    {0xA2U,
                     0U,
                     0xBDU,
                     0U,
                     static_cast<Byte>(0xE1U + page),
                     0x9DU,
                     0U,
                     static_cast<Byte>(2U + page),
                     0xE8U,
                     0xD0U,
                     0xF7U});
    }
    boot.insert(boot.end(), {0x4CU, 0U, 2U});
    for (unsigned bank = 0U; bank < 128U; ++bank) {
        const auto base = 16U + bank * 16384U;
        std::fill_n(image.begin() + base, 16384U, static_cast<Byte>(bank));
        std::ranges::copy(boot, image.begin() + base + 0x2000U);
        std::ranges::copy(code, image.begin() + base + 0x2100U);
        for (unsigned v = 0x3FFAU; v < 0x4000U; v += 2U) {
            image[base + v] = 0U;
            image[base + v + 1U] = 0xE0U;
        }
        std::fill_n(image.begin() + 16U + 2097152U + bank * 8192U, 8192U, static_cast<Byte>(bank));
    }
    return image;
}
} // namespace mapperbus::tests
