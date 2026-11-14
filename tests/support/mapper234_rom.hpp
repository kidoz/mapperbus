#pragma once

#include <algorithm>
#include <array>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::tests {

inline constexpr std::array<std::size_t, 3> kMaxi15PrgPages{16U, 32U, 32U};
inline constexpr std::array<std::size_t, 3> kMaxi15ChrPages{64U, 64U, 128U};

/// Original 6502 firmware; floating cases execute from CPU RAM at $0400.
inline std::vector<core::Byte> mapper234_program(unsigned population,
                                                 core::Byte outer,
                                                 bool write_outer,
                                                 bool floating = false) {
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
    const auto pwrite = [&store, &paddr](Address a, Byte v) {
        paddr(a);
        store(0x2007U, v);
    };
    const auto pread = [&code, &paddr, &expect](Address a, Byte v) {
        paddr(a);
        code.insert(code.end(), {0xADU, 7U, 0x20U});
        expect(0x2007U, v);
    };
    store(0x4017U, 0x40U);
    store(0x12U, 0U);
    store(0x13U, 0U);
    expect(0xFF80U, 0x80U);
    expect(0xFF81U, 0x40U);
    expect(0xFF82U, 0U);
    expect(0xFFC0U, 3U);
    store(0xFFDFU, 2U);
    if (write_outer)
        store(0xFF9FU, outer);
    else
        expect(0xFF88U, outer);
    if (floating) {
        expect(0x9A00U, 0x9AU);
        expect(0xFA00U, 0xFAU);
        pread(0x123U, population == 1U ? 0U : 0x23U);
        store(0xFFF7U, 0x10U);
        pread(0x123U, population == 1U ? 1U : 0x23U);
        expect(0xFFF0U, 0xFFU);
        pread(0x123U, population == 1U ? 3U : 0x23U);
    } else {
        expect(0xFF82U, 0U);
        expect(0xFFC1U, 0U); // Locked outer/CIC cannot change.
        for (unsigned i = 0U; i < 16U; ++i) {
            const auto inner = static_cast<Byte>(i / 2U * 16U + i % 2U);
            expect(static_cast<Address>(0xFFE8U + i), inner);
            const unsigned block = outer & 15U;
            const bool nina = (outer & 0x40U) != 0U;
            const unsigned prg =
                (nina ? block / 2U * 2U + i % 2U : block) + ((outer & 0x20U) != 0U ? 16U : 0U);
            const unsigned chr = (nina ? block / 2U * 8U + i / 2U : block * 4U + i / 2U % 4U) +
                                 ((outer & 0x20U) != 0U && population == 2U ? 64U : 0U);
            expect(0x9A00U, static_cast<Byte>(prg));
            expect(0xFA00U, static_cast<Byte>(prg));
            pread(0x123U, static_cast<Byte>(chr));
            pwrite(0x123U, 0xA7U);
            pread(0x123U, static_cast<Byte>(chr));
        }
    }
    pwrite(0x2000U, 0x19U);
    pwrite(0x2C00U, 0x27U);
    pread(0x2400U, (outer & 0x80U) != 0U ? 0x19U : 0x27U);
    pread(0x2800U, (outer & 0x80U) != 0U ? 0x27U : 0x19U);
    store(0x12U, floating ? 0xD7U : 0xC7U);
    const auto loop = static_cast<Address>((floating ? 0x400U : 0xE000U) + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    return code;
}

inline std::vector<core::Byte> mapper234_boot(unsigned population,
                                              core::Byte outer,
                                              bool write_outer,
                                              bool floating = false) {
    using core::Byte;
    auto code = mapper234_program(population, outer, write_outer, floating);
    if (!floating)
        return code;
    std::vector<Byte> boot;
    for (std::size_t i = 0U; i < code.size(); ++i) {
        const auto addr = static_cast<core::Address>(0x400U + i);
        boot.insert(
            boot.end(),
            {0xA9U, code[i], 0x8DU, static_cast<Byte>(addr), static_cast<Byte>(addr >> 8U)});
    }
    boot.insert(boot.end(), {0x4CU, 0U, 4U});
    return boot;
}

inline std::vector<core::Byte> make_mapper234_rom(unsigned population = 0U,
                                                  core::Byte outer = 0x0BU,
                                                  bool write_outer = false,
                                                  bool floating = false,
                                                  bool legacy = false,
                                                  bool vertical = true) {
    using core::Byte;
    const std::size_t prg_size = kMaxi15PrgPages[population] * 32768U;
    std::vector<Byte> data(16U + prg_size + kMaxi15ChrPages[population] * 8192U);
    data[0] = 'N';
    data[1] = 'E';
    data[2] = 'S';
    data[3] = 0x1AU;
    data[4] = static_cast<Byte>(prg_size / 16384U);
    data[5] = static_cast<Byte>(kMaxi15ChrPages[population]);
    data[6] = static_cast<Byte>(0xA0U | (vertical ? 1U : 0U));
    data[7] = legacy ? 0xE0U : 0xE8U;
    const auto boot = mapper234_boot(population, outer, write_outer, floating);
    for (std::size_t page = 0U; page < kMaxi15PrgPages[population]; ++page) {
        const std::size_t base = 16U + page * 32768U;
        std::fill_n(
            data.begin() + static_cast<std::ptrdiff_t>(base), 32768U, static_cast<Byte>(page));
        std::ranges::copy(boot, data.begin() + static_cast<std::ptrdiff_t>(base + 0x6000U));
        data[base + 0x7F80U] = 0x80U;
        data[base + 0x7F81U] = 0x40U;
        data[base + 0x7F82U] = 0U;
        data[base + 0x7F88U] = outer;
        data[base + 0x7F9FU] = 0xFFU;
        data[base + 0x7FC0U] = 3U;
        data[base + 0x7FC1U] = 0U;
        data[base + 0x7FDFU] = 0xFFU;
        for (std::size_t i = 0U; i < 16U; ++i)
            data[base + 0x7FE8U + i] = static_cast<Byte>(i / 2U * 16U + i % 2U);
        for (std::size_t v = base + 0x7FFAU; v < base + 32768U; v += 2U) {
            data[v] = 0U;
            data[v + 1U] = 0xE0U;
        }
    }
    for (std::size_t page = 0U; page < kMaxi15ChrPages[population]; ++page)
        std::fill_n(data.begin() + static_cast<std::ptrdiff_t>(16U + prg_size + page * 8192U),
                    8192U,
                    static_cast<Byte>(page));
    return data;
}

} // namespace mapperbus::tests
