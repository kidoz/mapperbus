#pragma once

#include <algorithm>
#include <cassert>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::tests {
// Original firmware, replicated in every 8 KiB page because PPU pins can change
// the executing PRG half. Page markers at $xA00 remain independent of the code.
inline std::vector<core::Byte> make_mapper245_rom(bool legacy = false,
                                                  bool vertical = true,
                                                  unsigned prg_size = 1048576U) {
    using core::Address;
    using core::Byte;
    std::vector<Byte> image(16U + prg_size);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = static_cast<Byte>(prg_size / 16384U);
    image[6] = static_cast<Byte>(0x52U | (vertical ? 1U : 0U));
    image[7] = legacy ? 0xF0U : 0xF8U;
    if (!legacy) {
        image[10] = 0x70U;
        image[11] = 7U;
    }
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
    const auto paddr = [&store](Address addr) {
        store(0x2006U, static_cast<Byte>(addr >> 8U));
        store(0x2006U, static_cast<Byte>(addr));
    };
    const auto reg = [&store](Byte index, Byte value, Byte mode) {
        store(0x8000U, static_cast<Byte>(index | mode));
        store(0x8001U, value);
    };
    store(0x4017U, 0x40U);
    store(0x2001U, 0U);
    store(0x11U, 0U);
    store(0x12U, 0U);
    code.insert(code.end(), {0xADU, 0U, 0x60U, 0x85U, 0x14U});
    paddr(0U);
    code.insert(code.end(), {0xADU, 7U, 0x20U, 0xADU, 7U, 0x20U, 0x85U, 0x15U});
    for (unsigned mode = 0U; mode < 4U; ++mode) {
        const auto control = static_cast<Byte>(mode << 6U);
        reg(6U, 3U, control);
        reg(7U, 5U, control);
        for (unsigned phase = 0U; phase < 2U; ++phase) {
            for (Byte r = 0U; r < 6U; ++r)
                reg(r, static_cast<Byte>(((r + phase) & 1U) * 2U), control);
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                paddr(static_cast<Address>(slot * 1024U));
                const unsigned selector = mode >= 2U ? slot + 2U : slot / 2U;
                const unsigned outer = ((selector + phase) & 1U) * 64U;
                const unsigned banks[4] = {
                    (mode & 1U) != 0U ? 62U : 3U, 5U, (mode & 1U) != 0U ? 3U : 62U, 63U};
                for (unsigned window = 0U; window < 4U; ++window)
                    expect(static_cast<Address>(0x9A00U + window * 8192U),
                           static_cast<Byte>((outer + banks[window]) % (prg_size / 8192U)));
            }
        }
    }
    paddr(0U);
    store(0x2007U, 0x5AU);
    paddr(0U);
    code.insert(code.end(), {0xADU, 7U, 0x20U});
    expect(0x2007U, 0x5AU);
    store(0x6000U, 0xA7U);
    store(0x7FFFU, 0x91U);
    // Existing MMC3 scanline IRQ compatibility path, exercised through the CPU.
    for (Byte r = 0U; r < 6U; ++r)
        reg(r, 2U, 0U);
    store(0xC000U, 2U);
    store(0xC001U, 0U);
    store(0xE001U, 0U);
    store(0x2000U, 8U);
    store(0x2001U, 0x18U);
    code.push_back(0x58U);
    code.insert(code.end(), {0xA5U, 0x11U, 0xC9U, 3U, 0x90U, 0xFAU, 0x78U});
    store(0xE000U, 0U);
    store(0x2001U, 0U);
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(), {0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    assert(code.size() < 0x1800U);
    const std::vector<Byte> handler{
        0x48U, 0xE6U, 0x11U, 0xA9U, 0U, 0x8DU, 0U, 0xE0U, 0x8DU, 1U, 0xE0U, 0x68U, 0x40U};
    for (unsigned bank = 0U; bank < prg_size / 8192U; ++bank) {
        const auto base = 16U + bank * 8192U;
        std::fill_n(image.begin() + base, 8192U, static_cast<Byte>(bank));
        std::ranges::copy(code, image.begin() + base);
        std::ranges::copy(handler, image.begin() + base + 0x1800U);
        for (unsigned v = 0x1FFAU; v < 0x2000U; v += 2U) {
            image[base + v] = 0U;
            image[base + v + 1U] = v == 0x1FFEU ? 0xF8U : 0xE0U;
        }
    }
    return image;
}
} // namespace mapperbus::tests
