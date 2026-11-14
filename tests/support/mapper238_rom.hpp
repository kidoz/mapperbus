#pragma once

#include <algorithm>
#include <array>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::tests {
/// Original fixed-bank firmware; exercises both cartridge bus routes and MMC3 IRQs.
inline std::vector<core::Byte> mapper238_program(bool vertical = true, core::Byte final_data = 3U) {
    using core::Address;
    using core::Byte;
    constexpr std::array<Byte, 4> answers{0U, 2U, 2U, 3U};
    std::vector<Byte> code{0x78U};
    const auto store = [&code](Address a, Byte v) {
        code.insert(code.end(),
                    {0xA9U, v, 0x8DU, static_cast<Byte>(a), static_cast<Byte>(a >> 8U)});
    };
    const auto expect = [&code](Address a, Byte v) {
        code.insert(
            code.end(),
            {0xADU, static_cast<Byte>(a), static_cast<Byte>(a >> 8U), 0xC9U, v, 0xD0U, 0xFEU});
    };
    const auto set_bank = [&store](Byte r, Byte v, Byte mode = 0U) {
        store(0x8000U, static_cast<Byte>(r | mode));
        store(0x8001U, v);
    };
    const auto paddr = [&store](Address a) {
        store(0x2006U, static_cast<Byte>(a >> 8U));
        store(0x2006U, static_cast<Byte>(a));
    };
    const auto pwrite = [&paddr, &store](Address a, Byte v) {
        paddr(a);
        store(0x2007U, v);
    };
    const auto pread = [&paddr, &code, &expect](Address a, Byte v) {
        paddr(a);
        code.insert(code.end(), {0xADU, 7U, 0x20U});
        expect(0x2007U, v);
    };
    store(0x4017U, 0x40U);
    store(0x11U, 0U);
    store(0x12U, 0U);
    store(0x13U, 0U);
    code.insert(code.end(), {0xADU, 0x20U, 0x40U, 0x85U, 0x14U});
    for (Address a : {0x4020U, 0x4FFFU, 0x5000U, 0x5FFFU, 0x6000U, 0x6FFFU, 0x7000U, 0x7FFFU})
        for (unsigned data = 0U; data < 4U; ++data) {
            store(a, static_cast<Byte>(0xFCU | data));
            expect(0x5000U, answers[data]);
            expect(0x7000U, answers[data]);
        }
    for (Byte mode : {0U, 0x40U}) {
        set_bank(6U, 3U, mode);
        set_bank(7U, 5U, mode);
        expect(0x9A00U, mode == 0U ? 3U : 14U);
        expect(0xBA00U, 5U);
        expect(0xDA00U, mode == 0U ? 14U : 3U);
        expect(0xFA00U, 15U);
    }
    set_bank(2U, 47U);
    pread(0x1000U, 47U);
    pwrite(0x1000U, 0U);
    pread(0x1000U, 47U);
    set_bank(2U, 47U, 0x80U);
    pread(0U, 47U);
    for (Byte gate : {0U, 0x40U, 0x80U, 0xC0U}) {
        store(0xA001U, gate);
        store(0x6000U, 1U);
        expect(0x4020U, 2U);
        expect(0x7FFFU, 2U);
    }
    store(0xA000U, 0U);
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    store(0xA000U, 1U);
    pread(0x2400U, 0x19U);
    pread(0x2800U, 0x27U);
    store(0xA000U, 0U);
    pread(0x2400U, 0x27U);
    pread(0x2800U, 0x19U);
    store(0xA000U, vertical ? 0U : 1U);
    store(0x4020U, final_data);
    expect(0x6000U, answers[final_data & 3U]);
    store(0xC000U, 2U);
    store(0xC001U, 0U);
    store(0xE001U, 0U);
    store(0x2000U, 8U);
    store(0x2001U, 0x18U);
    code.insert(code.end(), {0x58U, 0xEAU, 0xA5U, 0x11U, 0xC9U, 1U, 0x90U, 0xFAU});
    store(0xE000U, 0U);
    store(0xC000U, 0U);
    store(0xE001U, 0U);
    code.insert(code.end(), {0xA5U, 0x11U, 0xC9U, 3U, 0x90U, 0xFAU});
    store(0xE000U, 0U);
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    return code;
}
inline std::vector<core::Byte> make_mapper238_rom(bool legacy = false,
                                                  bool vertical = true,
                                                  core::Byte final_data = 3U) {
    using core::Byte;
    std::vector<Byte> bytes(16U + 131072U + 262144U);
    bytes[0] = 'N';
    bytes[1] = 'E';
    bytes[2] = 'S';
    bytes[3] = 0x1AU;
    bytes[4] = 8U;
    bytes[5] = 32U;
    bytes[6] = static_cast<Byte>(0xE0U | (vertical ? 1U : 0U));
    bytes[7] = legacy ? 0xE0U : 0xE8U;
    for (unsigned a = 0U; a < 131072U; ++a)
        bytes[16U + a] = static_cast<Byte>(a / 8192U);
    for (unsigned a = 0U; a < 262144U; ++a)
        bytes[16U + 131072U + a] = static_cast<Byte>(a / 1024U);
    const auto code = mapper238_program(vertical, final_data);
    const std::size_t base = 16U + 15U * 8192U;
    std::ranges::copy(code, bytes.begin() + static_cast<std::ptrdiff_t>(base));
    constexpr std::array<Byte, 13> irq{
        0x48U, 0xA9U, 0U, 0x8DU, 0U, 0xE0U, 0x8DU, 1U, 0xE0U, 0xE6U, 0x11U, 0x68U, 0x40U};
    std::ranges::copy(irq, bytes.begin() + static_cast<std::ptrdiff_t>(base + 0x1800U));
    for (std::size_t v = base + 0x1FFAU; v < base + 8192U; v += 2U) {
        bytes[v] = 0U;
        bytes[v + 1U] = v == base + 0x1FFCU ? 0xE0U : 0xF8U;
    }
    return bytes;
}
} // namespace mapperbus::tests
