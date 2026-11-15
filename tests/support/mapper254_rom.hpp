#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::tests {
inline std::vector<core::Byte> mapper254_prg() {
    std::vector<core::Byte> data(131072U);
    for (unsigned bank = 0U; bank < 16U; ++bank)
        std::fill_n(data.begin() + bank * 8192U, 8192U, static_cast<core::Byte>(bank));
    return data;
}
inline std::vector<core::Byte> mapper254_chr() {
    std::vector<core::Byte> data(131072U);
    for (unsigned bank = 0U; bank < 128U; ++bank)
        std::fill_n(data.begin() + bank * 1024U, 1024U, static_cast<core::Byte>(bank));
    return data;
}
// Original firmware checks protection before any MMC3 bank-select write can
// unlock it, then exercises banks, PPUDATA/CIRAM and actual CPU IRQ delivery.
inline std::vector<core::Byte> make_mapper254_rom(bool legacy = false,
                                                  bool battery = false,
                                                  bool vertical = true) {
    using core::Address;
    using core::Byte;
    std::vector<Byte> image(16U + 262144U);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = 8U;
    image[5] = 16U;
    image[6] = static_cast<Byte>(0xE0U | (battery ? 2U : 0U) | (vertical ? 1U : 0U));
    image[7] = legacy ? 0xF0U : 0xF8U;
    if (!legacy)
        image[10] = battery ? 0x70U : 7U;
    const auto prg = mapper254_prg(), chr = mapper254_chr();
    std::ranges::copy(prg, image.begin() + 16);
    std::ranges::copy(chr, image.begin() + 16 + 131072);
    std::vector<Byte> code{0x78U, 0xD8U, 0xA2U, 0xFFU, 0x9AU};
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
    const auto reg = [&store](Byte index, Byte value, Byte mode) {
        store(0x8000U, static_cast<Byte>(index | mode));
        store(0x8001U, value);
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
    store(0x4017U, 0x40U);
    store(0x11U, 0U);
    store(0x12U, 0U);
    code.insert(code.end(), {0xADU, 0U, 0x60U, 0x85U, 0x14U});
    store(0xBFFFU, 0x80U);
    code.insert(code.end(), {0xADU, 0U, 0x60U, 0x85U, 0x15U});
    store(0x6000U, 0xA5U);
    for (Byte mask : {0x80U, 0x95U, 0xA3U, 0xBFU, 0xC0U, 0xFFU}) {
        store(0xA001U, mask);
        expect(0x6000U, static_cast<Byte>(0xA5U ^ mask));
    }
    store(0x6000U, 0U); // Last mask has WRAM write-protected.
    expect(0x6000U, 0x5AU);
    store(0x9FFEU, 0xC6U); // Alias unlock also performs MMC3 bank select.
    expect(0x6000U, 0xA5U);
    store(0xBFFFU, 0x95U);
    expect(0x6000U, 0xA5U);
    store(0x6000U, 0x5AU);
    expect(0x6000U, 0x5AU);
    for (Byte mode : {0U, 0x40U})
        for (Byte bank : {0U, 3U, 7U, 15U, 255U}) {
            reg(6U, bank, mode);
            reg(7U, 5U, mode);
            expect(0x9A00U, mode == 0U ? bank & 15U : 14U);
            expect(0xBA00U, 5U);
            expect(0xDA00U, mode == 0U ? 14U : bank & 15U);
            expect(0xFA00U, 15U);
        }
    for (Byte inversion : {0U, 0x80U}) {
        constexpr std::array<Byte, 6> kChr{6U, 10U, 13U, 17U, 19U, 23U};
        for (Byte r = 0U; r < 6U; ++r)
            reg(r, kChr[r], inversion);
        constexpr std::array<Byte, 8> kPages{6U, 7U, 10U, 11U, 13U, 17U, 19U, 23U};
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            const auto addr = static_cast<Address>(slot * 1024U);
            paddr(addr);
            store(0x2007U, 0xFFU);
            pread(addr, kPages[slot ^ (inversion == 0U ? 0U : 4U)]);
        }
    }
    store(0xA000U, 0U);
    paddr(0x2000U);
    store(0x2007U, 0x19U);
    paddr(0x2800U);
    store(0x2007U, 0x27U);
    pread(0x2000U, 0x27U);
    store(0xA000U, 1U);
    paddr(0x2400U);
    store(0x2007U, 0x35U);
    pread(0x2000U, 0x35U);
    store(0xC000U, 2U);
    store(0xC001U, 0U);
    store(0xE001U, 0U);
    store(0x2000U, 8U);
    store(0x2001U, 0x18U);
    code.push_back(0x58U);
    code.insert(code.end(), {0xA5U, 0x11U, 0xC9U, 3U, 0x90U, 0xFAU, 0x78U});
    store(0xE000U, 0U);
    store(0x2001U, 0U);
    store(0xA001U, 0x95U);
    store(0x6000U, 0xA7U);
    store(0x7FFFU, 0x91U);
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    assert(code.size() < 0x1000U);
    const auto base = 16U + 15U * 8192U;
    std::ranges::copy(code, image.begin() + base);
    const std::array<Byte, 13> handler{
        0x48U, 0xE6U, 0x11U, 0xA9U, 0U, 0x8DU, 0U, 0xE0U, 0x8DU, 1U, 0xE0U, 0x68U, 0x40U};
    std::ranges::copy(handler, image.begin() + base + 0x1000U);
    for (unsigned v = 0x1FFAU; v < 0x2000U; v += 2U) {
        image[base + v] = 0U;
        image[base + v + 1U] = v == 0x1FFCU ? 0xE0U : 0xF0U;
    }
    return image;
}
} // namespace mapperbus::tests
