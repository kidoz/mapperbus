#pragma once

#include <algorithm>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::tests {
inline core::Byte mapper237_marker(unsigned page, unsigned offset) {
    return static_cast<core::Byte>(page * 7U + offset / 4U * 5U + offset % 4U);
}
inline unsigned mapper237_low_page(unsigned mode, unsigned inner) {
    return mode == 1U || mode == 3U ? inner / 2U * 2U : inner;
}
inline unsigned mapper237_high_page(unsigned mode, unsigned inner) {
    return mode < 2U ? 7U : (mode == 2U ? inner : inner / 2U * 2U + 1U);
}

/// Original RAM-resident firmware: type mode also affects ROM opcode fetches.
inline std::vector<core::Byte> mapper237_program(unsigned mode = 0U,
                                                 bool type = false,
                                                 unsigned outer = 7U) {
    using core::Address;
    using core::Byte;
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
    const auto paddr = [&store](Address a) {
        store(0x2006U, static_cast<Byte>(a >> 8U));
        store(0x2006U, static_cast<Byte>(a));
    };
    const auto pwrite = [&paddr, &store](Address a, Byte v) {
        paddr(a);
        store(0x2007U, v);
    };
    const auto pread = [&code, &paddr, &expect](Address a, Byte v) {
        paddr(a);
        code.insert(code.end(), {0xADU, 7U, 0x20U});
        expect(0x2007U, v);
    };
    const auto check_banks = [&expect, mode, type, outer](unsigned inner) {
        const auto lo = outer * 8U + mapper237_low_page(mode, inner);
        const auto hi = outer * 8U + mapper237_high_page(mode, inner);
        expect(0x9A80U, mapper237_marker(lo, type ? 0x1A82U : 0x1A80U));
        expect(0xFA81U, mapper237_marker(hi, type ? 0x3A83U : 0x3A81U));
    };
    store(0x4017U, 0x40U);
    store(0x12U, 0U);
    store(0x13U, 0U);
    paddr(0x123U);
    code.insert(code.end(), {0xADU, 7U, 0x20U, 0xADU, 7U, 0x20U, 0x85U, 0x14U});
    pwrite(0x123U, 0x5AU);
    store(0x8001U, 0U);
    expect(0xC000U, 2U); // Synthetic 00/01/02/03 table reproduces the reported probe.
    expect(0x9A80U, mapper237_marker(0U, 0x1A82U));
    expect(0x9A81U, mapper237_marker(0U, 0x1A83U));
    store(0x8000U, 0U);
    const auto address = static_cast<Address>(0x8000U + outer / 4U * 4U + (type ? 1U : 0U));
    const auto upper = static_cast<Byte>(mode * 64U + 32U + outer % 4U * 8U);
    for (unsigned inner = 0U; inner < 8U; ++inner) {
        store(address, static_cast<Byte>(upper + inner));
        check_banks(inner);
    }
    pread(0x123U, 0x5AU);
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    store(address, static_cast<Byte>(upper ^ 32U));
    pread(0x2400U, 0x19U);
    pread(0x2800U, 0x27U);
    store(address, upper);
    pread(0x2400U, 0x27U);
    pread(0x2800U, 0x19U);
    store(static_cast<Address>(address + 2U), upper); // Capture the lock and all configuration.
    for (unsigned inner = 0U; inner < 8U; ++inner) {
        store(static_cast<Address>(address ^ 7U), static_cast<Byte>((upper ^ 0xF8U) + inner));
        check_banks(inner);
    }
    pread(0x2400U, 0x27U);
    pread(0x2800U, 0x19U);
    pread(0x123U, 0x5AU);
    store(0xFFFFU, 6U);
    check_banks(6U);
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0x200U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    return code;
}
inline std::vector<core::Byte> mapper237_boot(unsigned mode = 0U,
                                              bool type = false,
                                              unsigned outer = 7U) {
    using core::Byte;
    const auto code = mapper237_program(mode, type, outer);
    std::vector<Byte> boot;
    for (std::size_t i = 0U; i < code.size(); ++i) {
        const auto a = static_cast<core::Address>(0x200U + i);
        boot.insert(boot.end(),
                    {0xA9U, code[i], 0x8DU, static_cast<Byte>(a), static_cast<Byte>(a >> 8U)});
    }
    boot.insert(boot.end(), {0x4CU, 0U, 2U});
    return boot;
}
inline std::vector<core::Byte> make_mapper237_rom(unsigned mode = 0U,
                                                  bool type = false,
                                                  unsigned outer = 7U,
                                                  bool legacy = false,
                                                  bool vertical = true) {
    using core::Byte;
    std::vector<Byte> bytes(16U + 1048576U);
    bytes[0] = 'N';
    bytes[1] = 'E';
    bytes[2] = 'S';
    bytes[3] = 0x1AU;
    bytes[4] = 64U;
    bytes[6] = static_cast<Byte>(0xD0U | (vertical ? 1U : 0U));
    bytes[7] = legacy ? 0xE0U : 0xE8U;
    if (!legacy)
        bytes[11] = 7U;
    const auto boot = mapper237_boot(mode, type, outer);
    for (unsigned page = 0U; page < 64U; ++page) {
        const std::size_t base = 16U + page * 16384U;
        for (unsigned a = 0U; a < 16384U; ++a)
            bytes[base + a] = mapper237_marker(page, a);
        std::ranges::copy(boot, bytes.begin() + static_cast<std::ptrdiff_t>(base + 0x2000U));
        for (std::size_t v = base + 0x3FFAU; v < base + 16384U; v += 2U) {
            bytes[v] = 0U;
            bytes[v + 1U] = 0xE0U;
        }
    }
    for (unsigned n = 0U; n < 4U; ++n)
        bytes[16U + 7U * 16384U + n] = static_cast<Byte>(n);
    return bytes;
}
} // namespace mapperbus::tests
