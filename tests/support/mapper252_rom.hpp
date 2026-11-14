#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::tests {
inline std::vector<core::Byte> mapper252_prg() {
    std::vector<core::Byte> data(262144U);
    for (unsigned bank = 0U; bank < 32U; ++bank)
        std::fill_n(data.begin() + bank * 8192U, 8192U, static_cast<core::Byte>(bank));
    return data;
}
inline std::vector<core::Byte> mapper252_chr() {
    std::vector<core::Byte> data(131072U);
    for (unsigned bank = 0U; bank < 128U; ++bank)
        std::fill_n(data.begin() + bank * 1024U, 1024U, static_cast<core::Byte>(bank));
    return data;
}
// Original 6502 firmware; rendering stays off so IRQ delivery proves CPU clocks
// drive both VRC counter modes. The fixed $E000 page holds code and vectors.
inline std::vector<core::Byte> make_mapper252_rom(bool legacy = false,
                                                  bool battery = true,
                                                  bool vertical = true) {
    using core::Address;
    using core::Byte;
    std::vector<Byte> image(16U + 262144U + 131072U);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = 16U;
    image[5] = 16U;
    image[6] = static_cast<Byte>(0xC0U | (battery ? 2U : 0U) | (vertical ? 1U : 0U));
    image[7] = legacy ? 0xF0U : 0xF8U;
    if (!legacy) {
        image[10] = battery ? 0x70U : 7U;
        image[11] = 5U;
    }
    const auto prg = mapper252_prg(), chr = mapper252_chr();
    std::ranges::copy(prg, image.begin() + 16);
    std::ranges::copy(chr, image.begin() + 16 + 262144);
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
    const auto pread = [&paddr, &code, &expect](Address addr, Byte value) {
        paddr(addr);
        code.insert(code.end(), {0xADU, 7U, 0x20U});
        expect(0x2007U, value);
    };
    const auto pwrite = [&paddr, &store](Address addr, Byte value) {
        paddr(addr);
        store(0x2007U, value);
    };
    const auto chr_bank = [&store](unsigned slot, unsigned bank) {
        const auto reg = static_cast<Address>(0xB000U + (slot / 2U) * 4096U + (slot & 1U) * 8U);
        store(reg, static_cast<Byte>(bank & 15U));
        store(reg + 4U, static_cast<Byte>(bank >> 4U));
    };
    store(0x4017U, 0x40U);
    store(0x11U, 0U);
    store(0x12U, 0U);
    store(0x9008U, 1U);
    code.insert(code.end(), {0xADU, 0U, 0x60U, 0x85U, 0x14U});
    chr_bank(0U, 6U);
    paddr(0U);
    code.insert(code.end(), {0xADU, 7U, 0x20U, 0xADU, 7U, 0x20U, 0x85U, 0x15U});
    for (Byte mode : {1U, 3U})
        for (Byte bank : {0U, 7U, 15U, 31U, 255U}) {
            store(0x9008U, mode);
            store(0x8000U, bank);
            store(0xA000U, 5U);
            expect(0x9A00U, mode == 1U ? bank & 31U : 30U);
            expect(0xBA00U, 5U);
            expect(0xDA00U, mode == 1U ? 30U : bank & 31U);
            expect(0xFA00U, 31U);
        }
    for (unsigned bank : {6U, 7U, 5U, 0x106U})
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            chr_bank(slot, bank);
            pwrite(static_cast<Address>(slot * 1024U), bank == 7U ? 0xA6U : 0x5AU);
            pread(static_cast<Address>(slot * 1024U),
                  bank == 6U   ? 0x5AU
                  : bank == 7U ? 0xA6U
                               : bank & 127U);
        }
    for (Byte mode = 0U; mode < 4U; ++mode) {
        store(0x9000U, mode);
        for (unsigned table = 0U; table < 4U; ++table)
            pwrite(static_cast<Address>(0x2000U + table * 1024U),
                   static_cast<Byte>(0x19U + table * 8U));
        pread(0x2000U, mode == 0U ? 0x29U : mode == 1U ? 0x21U : 0x31U);
        pread(0x2400U, mode == 1U ? 0x21U : 0x31U);
    }
    for (unsigned phase = 0U; phase < 2U; ++phase) {
        store(0xF000U, phase == 0U ? 0U : 15U);
        store(0xF004U, phase == 0U ? 13U : 15U);
        store(0xF008U, phase == 0U ? 7U : 3U);
        code.push_back(0x58U);
        code.insert(
            code.end(),
            {0xA5U, 0x11U, 0xC9U, static_cast<Byte>(phase == 0U ? 3U : 6U), 0x90U, 0xFAU, 0x78U});
        store(0xF008U, 0U);
    }
    store(0x9008U, 1U);
    chr_bank(0U, 6U);
    store(0x6000U, 0xA7U);
    store(0x7FFFU, 0x91U);
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    assert(code.size() < 0x1000U);
    const auto base = 16U + 31U * 8192U;
    std::ranges::copy(code, image.begin() + base);
    const std::array<Byte, 11> handler{
        0x48U, 0xE6U, 0x11U, 0xA9U, 0U, 0x8DU, 0x0CU, 0xF0U, 0x68U, 0x40U, 0xEAU};
    std::ranges::copy(handler, image.begin() + base + 0x1000U);
    for (unsigned v = 0x1FFAU; v < 0x2000U; v += 2U) {
        image[base + v] = 0U;
        image[base + v + 1U] = v == 0x1FFCU ? 0xE0U : 0xF0U;
    }
    return image;
}
} // namespace mapperbus::tests
