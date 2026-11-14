#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::tests {
inline std::vector<core::Byte> mapper246_prg() {
    std::vector<core::Byte> prg(524288U);
    for (unsigned bank = 0U; bank < 64U; ++bank)
        std::fill_n(prg.begin() + bank * 8192U, 8192U, static_cast<core::Byte>(bank));
    return prg;
}
inline std::vector<core::Byte> mapper246_chr() {
    std::vector<core::Byte> chr(524288U);
    for (unsigned bank = 0U; bank < 256U; ++bank)
        std::fill_n(chr.begin() + bank * 2048U, 2048U, static_cast<core::Byte>(bank));
    return chr;
}
// Original firmware, identical in all PRG pages. A BRK checks actual vector
// reads: pages with A17 low contain an intentionally wrong IRQ vector.
inline std::vector<core::Byte> make_mapper246_rom(bool legacy = false, bool vertical = true) {
    using core::Address;
    using core::Byte;
    std::vector<Byte> image(16U + 1048576U);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = 32U;
    image[5] = 64U;
    image[6] = static_cast<Byte>(0x62U | (vertical ? 1U : 0U));
    image[7] = legacy ? 0xF0U : 0xF8U;
    if (!legacy)
        image[10] = 0x50U;
    const auto prg = mapper246_prg(), chr = mapper246_chr();
    std::ranges::copy(prg, image.begin() + 16);
    std::ranges::copy(chr, image.begin() + 16 + 524288);
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
    store(0x4017U, 0x40U);
    store(0x11U, 0U);
    store(0x12U, 0U);
    code.insert(code.end(), {0xADU, 0U, 0xFAU, 0x85U, 0x14U});
    paddr(0U);
    code.insert(code.end(),
                {0xADU, 7U, 0x20U, 0xADU, 7U, 0x20U, 0x85U, 0x15U, 0xADU, 0U, 0x68U, 0x85U, 0x16U});
    for (Byte bank : {0U, 15U, 16U, 31U, 32U, 47U, 48U, 63U, 255U}) {
        for (unsigned window = 0U; window < 4U; ++window) {
            store(static_cast<Address>(0x6018U + window), bank);
            expect(static_cast<Address>(0x9A00U + window * 8192U), bank & 63U);
        }
        expect(0xFFE4U, static_cast<Byte>((bank & 63U) | 16U));
        expect(0xFFE8U, bank & 63U);
    }
    for (Byte bank : {0U, 1U, 15U, 31U, 63U, 127U, 128U, 255U})
        for (unsigned window = 0U; window < 4U; ++window) {
            store(static_cast<Address>(0x601CU + window), bank);
            pread(static_cast<Address>(window * 2048U), bank);
        }
    const std::array<Byte, 4> prg_end{3U, 5U, 7U, 32U}, chr_end{15U, 31U, 127U, 255U};
    for (unsigned window = 0U; window < 4U; ++window) {
        store(static_cast<Address>(0x6000U + window), prg_end[window]);
        store(static_cast<Address>(0x6004U + window), chr_end[window]);
    }
    store(0x6020U, 0U); // Outside the actual GAL write decode.
    store(0x67FCU, 0U);
    expect(0x9A00U, 3U);
    pwrite(0U, 0xFFU);
    pread(0U, 15U);
    for (unsigned table = 0U; table < 4U; ++table)
        pwrite(static_cast<Address>(0x2000U + table * 1024U),
               static_cast<Byte>(0x19U + table * 8U));
    pread(0x2000U, vertical ? 0x29U : 0x21U);
    pread(0x2400U, vertical ? 0x31U : 0x21U);
    code.insert(code.end(), {0x00U, 0xEAU}); // BRK vector must come from page 48, not page 32.
    expect(0x11U, 1U);
    expect(0xFA00U, 32U); // Vector override never alters the stored bank.
    store(0x6800U, 0xA7U);
    store(0x6FFFU, 0x91U);
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    assert(code.size() < 0x1000U);
    for (unsigned bank = 0U; bank < 64U; ++bank) {
        const auto base = 16U + bank * 8192U;
        std::ranges::copy(code, image.begin() + base);
        image[base + 0x1000U] = 0xE6U; // INC $11; RTI
        image[base + 0x1001U] = 0x11U;
        image[base + 0x1002U] = 0x40U;
        image[base + 0x1100U] = 0x4CU; // Wrong vector traps at $F100.
        image[base + 0x1101U] = 0U;
        image[base + 0x1102U] = 0xF1U;
        for (unsigned v = 0x1FFAU; v < 0x2000U; v += 2U) {
            image[base + v] = 0U;
            image[base + v + 1U] = v == 0x1FFEU ? ((bank & 16U) != 0U ? 0xF0U : 0xF1U) : 0xE0U;
        }
    }
    return image;
}
} // namespace mapperbus::tests
