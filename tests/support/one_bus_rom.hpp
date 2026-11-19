#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::tests {
// Synthetic mapper 256 firmware for the native VT02 banking and IRQ paths.
inline std::vector<core::Byte> make_one_bus_component_rom(bool shared, bool vertical = true) {
    using core::Address;
    using core::Byte;
    std::vector<Byte> image(16U + 131072U + (shared ? 0U : 131072U));
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = 8U;
    image[5] = shared ? 0U : 16U;
    image[6] = static_cast<Byte>(vertical ? 1U : 0U);
    image[7] = 11U;
    image[8] = 1U;
    image[13] = 6U;
    for (unsigned i = 0U; i < 131072U; ++i)
        image[16U + i] = static_cast<Byte>(i / (shared ? 1024U : 8192U));
    if (!shared)
        for (unsigned i = 0U; i < 131072U; ++i)
            image[16U + 131072U + i] = static_cast<Byte>(i / 1024U);
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
    const auto bank_expect = [&expect, shared](Address addr, Byte bank) {
        expect(addr, static_cast<Byte>(shared ? bank * 8U + 6U : bank));
    };
    const auto paddr = [&store](Address addr) {
        store(0x2006U, static_cast<Byte>(addr >> 8U));
        store(0x2006U, static_cast<Byte>(addr));
    };
    const auto pread = [&code, &expect, &paddr](Address addr, Byte value) {
        paddr(addr);
        code.insert(code.end(), {0xADU, 7U, 0x20U});
        expect(0x2007U, value);
    };
    store(0x4017U, 0x40U);
    store(0x2000U, 0U);
    store(0x2001U, 0U);
    store(0x12U, 0U);
    store(0x4107U, 3U);
    store(0x4108U, 5U);
    store(0x4109U, 9U);
    store(0x410BU, 0x40U);
    for (Byte control : {0U, 0x40U}) {
        store(0x4105U, control);
        bank_expect(0x9A00U, control == 0U ? 3U : 9U);
        bank_expect(0xBA00U, 5U);
        bank_expect(0xDA00U, control == 0U ? 9U : 3U);
        bank_expect(0xFA00U, 15U);
    }
    store(0x4105U, 0x46U);
    store(0x8001U, 7U);
    store(0x9FFEU, 0x47U);
    store(0x9FFFU, 11U);
    bank_expect(0x9A00U, 9U);
    bank_expect(0xBA00U, 11U);
    bank_expect(0xDA00U, 7U);
    bank_expect(0xFA00U, 15U);
    store(0x410BU, 0x48U); // Forwarding disabled, PQ2 still enabled.
    store(0x8000U, 6U);
    store(0x8001U, 0U);
    store(0xA000U, 1U);
    bank_expect(0xDA00U, 7U);
    store(0x4107U, 13U);
    bank_expect(0xDA00U, 13U);
    store(0x410BU, 0U);
    for (Byte inversion : {0U, 0x80U}) {
        store(0x4105U, inversion);
        constexpr std::array<Byte, 6> kChr{13U, 17U, 19U, 23U, 6U, 10U};
        for (unsigned r = 0U; r < 6U; ++r)
            store(static_cast<Address>(0x2012U + r), kChr[r]);
        constexpr std::array<Byte, 8> kPages{6U, 7U, 10U, 11U, 13U, 17U, 19U, 23U};
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            const auto addr = static_cast<Address>(slot * 1024U);
            paddr(addr);
            store(0x2007U, 0xFFU);
            pread(addr, kPages[slot ^ (inversion == 0U ? 0U : 4U)]);
        }
    }
    store(0x4105U, 0x20U);
    store(0x8000U, 0U); // Forwarding must preserve native CIRAM mode.
    paddr(0U);
    store(0x2007U, 0x5AU);
    pread(0x800U, 0x5AU);
    paddr(0x400U);
    store(0x2007U, 0xA6U);
    pread(0x1C00U, 0xA6U);
    store(0x4105U, 0U);
    pread(0U, 6U); // Restore ROM; CIRAM did not change the source ROM.
    for (Byte mirror : {0U, 1U}) {
        store(0x4106U, mirror);
        paddr(0x2000U);
        store(0x2007U, 0x19U);
        paddr(0x2400U);
        store(0x2007U, 0x27U);
        pread(0x2000U, mirror == 0U ? 0x19U : 0x27U);
    }
    for (Byte mode : {0x80U, 0U}) {
        store(0x4103U, 0U);
        store(0x11U, 0U);
        store(0x410BU, mode);
        store(0x4101U, 2U);
        store(0x4102U, 0U);
        store(0x4104U, 0U);
        store(0x2000U, 8U);
        store(0x2001U, mode == 0U ? 0x18U : 0U);
        code.insert(code.end(), {0x58U, 0xA5U, 0x11U, 0xC9U, 3U, 0x90U, 0xFAU, 0x78U});
        if (mode != 0U)
            code.insert(code.end(), {0xA5U, 0x11U, 0x85U, 0x14U});
    }
    store(0x4103U, 0U);
    store(0x2001U, 0U);
    store(0x12U, 0xC8U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    assert(code.size() < 0x1000U);
    const auto base = 16U + 15U * 8192U;
    std::ranges::copy(code, image.begin() + base);
    const std::array<Byte, 13> handler{
        0x48U, 0xE6U, 0x11U, 0xA9U, 0U, 0x8DU, 3U, 0x41U, 0x8DU, 4U, 0x41U, 0x68U, 0x40U};
    std::ranges::copy(handler, image.begin() + base + 0x1000U);
    for (unsigned v = 0x1FFAU; v < 0x2000U; v += 2U) {
        image[base + v] = 0U;
        image[base + v + 1U] = v == 0x1FFCU ? 0xE0U : 0xF0U;
    }
    return image;
}
} // namespace mapperbus::tests
