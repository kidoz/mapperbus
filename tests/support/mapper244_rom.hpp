#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::tests {
// Independent bank truth tables, checked against both published references.
inline constexpr std::array<std::array<core::Byte, 4>, 4> k244Prg{
    {{0, 1, 2, 3}, {3, 2, 1, 0}, {0, 2, 1, 3}, {3, 1, 2, 0}}};
inline constexpr std::array<std::array<core::Byte, 8>, 8> k244Chr{{{0, 1, 2, 3, 4, 5, 6, 7},
                                                                   {0, 2, 1, 3, 4, 6, 5, 7},
                                                                   {0, 1, 4, 5, 2, 3, 6, 7},
                                                                   {0, 4, 1, 5, 2, 6, 3, 7},
                                                                   {0, 4, 2, 6, 1, 5, 3, 7},
                                                                   {0, 2, 4, 6, 1, 3, 5, 7},
                                                                   {7, 6, 5, 4, 3, 2, 1, 0},
                                                                   {7, 6, 5, 4, 3, 2, 1, 0}}};
inline std::vector<core::Byte> mapper244_prg() {
    using core::Byte;
    std::vector<Byte> prg(131072U);
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        const auto base = bank * 32768U;
        std::fill_n(prg.begin() + base, 32768U, static_cast<Byte>(bank));
        for (unsigned i = 0U; i < 256U; ++i)
            prg[base + 0x100U + i] = static_cast<Byte>(i);
        prg[base + 0x200U] = 0xFFU;
        prg[base + 0x300U] = bank == 0U ? 2U : bank == 2U ? 1U : 0xFFU;
    }
    return prg;
}
inline std::vector<core::Byte> mapper244_chr() {
    std::vector<core::Byte> chr(65536U);
    for (unsigned bank = 0U; bank < 8U; ++bank)
        std::fill_n(chr.begin() + bank * 8192U, 8192U, static_cast<core::Byte>(bank));
    return chr;
}
/// Original replicated firmware: each PRG bank has identical code and vectors.
inline std::vector<core::Byte> make_mapper244_rom(bool legacy = false, bool vertical = true) {
    using core::Address;
    using core::Byte;
    std::vector<Byte> bytes(16U + 131072U + 65536U, 0U);
    bytes[0] = 'N';
    bytes[1] = 'E';
    bytes[2] = 'S';
    bytes[3] = 0x1AU;
    bytes[4] = 8U;
    bytes[5] = 8U;
    bytes[6] = static_cast<Byte>(0x40U | (vertical ? 1U : 0U));
    bytes[7] = legacy ? 0xF0U : 0xF8U;
    const auto prg = mapper244_prg(), chr = mapper244_chr();
    std::ranges::copy(prg, bytes.begin() + 16);
    std::ranges::copy(chr, bytes.begin() + 16 + 131072);
    std::vector<Byte> code{0x78U};
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
    const auto pwrite = [&paddr, &store](Address addr, Byte value) {
        paddr(addr);
        store(0x2007U, value);
    };
    const auto pread = [&paddr, &code, &expect](Address addr, Byte value) {
        paddr(addr);
        code.insert(code.end(), {0xADU, 7U, 0x20U});
        expect(0x2007U, value);
    };
    store(0x4017U, 0x40U);
    store(0x12U, 0U);
    store(0x13U, 0U);
    code.insert(code.end(), {0xADU, 0U, 0x9AU, 0x85U, 0x14U});
    paddr(0U);
    code.insert(code.end(), {0xADU, 7U, 0x20U, 0xADU, 7U, 0x20U, 0x85U, 0x15U});
    for (unsigned mode = 0; mode < 4U; ++mode)
        for (unsigned bank = 0; bank < 4U; ++bank) {
            store(0x8200U, static_cast<Byte>((mode << 4U) | bank));
            expect(0x9A00U, k244Prg[mode][bank]);
        }
    for (unsigned mode = 0; mode < 8U; ++mode)
        for (unsigned bank = 0; bank < 8U; ++bank) {
            store(0x8200U, static_cast<Byte>((mode << 4U) | 8U | bank));
            pread(0U, k244Chr[mode][bank]);
            expect(0x9A00U, 0U);
        }
    store(0x8200U, 0x0DU);
    store(0x8100U, 0x2FU);
    expect(0x9A00U, 0U);
    pread(0U, 5U);
    store(0x810BU, 0x7FU);
    pread(0U, 3U);
    expect(0x9A00U, 0U);
    store(0x8200U, 0U);
    store(0x8200U, 0x0DU);
    code.insert(code.end(), {0xEEU, 0U, 0x83U});
    expect(0x9A00U, 1U);
    pread(0U, 5U);
    pwrite(0U, 0xFFU);
    pread(0U, 5U);
    store(0x8200U, 3U);
    store(0x8200U, 0x0FU);
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    pread(0x2000U, vertical ? 0x19U : 0x27U);
    pread(0x2800U, vertical ? 0x19U : 0U);
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    assert(code.size() < 0x1800U);
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        const auto base = 16U + bank * 32768U;
        std::ranges::copy(code, bytes.begin() + base + 0x6000U);
        for (unsigned v = 0x7FFAU; v < 0x8000U; v += 2U) {
            bytes[base + v] = 0U;
            bytes[base + v + 1U] = 0xE0U;
        }
    }
    return bytes;
}
} // namespace mapperbus::tests
