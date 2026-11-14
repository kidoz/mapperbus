#pragma once

#include <algorithm>
#include <array>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::tests {

/// Original 6502 firmware for reset-42, disch-233 and everynes-vector profiles.
inline std::vector<core::Byte> mapper233_program(unsigned profile) {
    using core::Address;
    using core::Byte;
    const bool disch = profile == 1U;
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
    code.insert(code.end(), {0xADU, 0U, 0x9AU, 0x85U, 0x14U}); // Initial PRG selection.
    paddr(0x123U);
    code.insert(code.end(), {0xADU, 7U, 0x20U, 0xADU, 7U, 0x20U, 0x85U, 0x15U});
    store(0x8001U, 0U);
    pwrite(0x123U, 0x39U);
    store(0x8001U, 2U);
    pwrite(0x123U, 0xA7U);
    pread(0x123U, disch ? 0xA7U : 0x39U);
    store(0x8001U, 0U);
    pwrite(0x123U, 0x5AU);
    pread(0x123U, 0x5AU);
    for (unsigned outer = 0U; outer < (disch ? 1U : 2U); ++outer)
        for (unsigned page : {0U, 1U, 17U, 31U})
            for (bool small : {false, true})
                for (unsigned mode = 0U; mode < (disch ? 4U : 2U); ++mode) {
                    const auto value =
                        static_cast<Byte>(page + (small ? 32U : 0U) + mode * 64U + outer * 128U);
                    store(0x8000U, value);
                    const unsigned low = small ? page : page / 2U * 2U;
                    const unsigned high = small ? page : page / 2U * 2U + 1U;
                    expect(0x9A00U, static_cast<Byte>(outer * 32U + low));
                    expect(0xFA00U, static_cast<Byte>(outer * 32U + high));
                }
    store(0x8000U, 0x40U);
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    constexpr std::array<std::array<unsigned, 4>, 4> kPages{
        {{0U, 0U, 0U, 1U}, {0U, 1U, 0U, 1U}, {0U, 0U, 1U, 1U}, {1U, 1U, 1U, 1U}}};
    for (unsigned mode = 0U; mode < (disch ? 4U : 2U); ++mode) {
        store(0x8000U, static_cast<Byte>(mode * 64U));
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            const unsigned page = disch ? kPages[mode][slot] : (mode != 0U ? slot % 2U : slot / 2U);
            pread(static_cast<Address>(0x2000U + slot * 1024U), page != 0U ? 0x27U : 0x19U);
        }
    }
    if (profile == 2U) {
        store(0x8000U, 0U);
        expect(0xFFFDU, 0xE0U); // Old bank supplies the vector byte before the latch changes.
        expect(0x9A00U, 63U);
        expect(0xFA00U, 63U);
    }
    if (profile == 0U) {
        // Restore the initially selected A19 so the following reset alternates
        // relative to the mode this firmware entered with.
        code.insert(code.end(),
                    {0xA5U, 0x14U, 0x29U, 0x20U, 0x0AU, 0x0AU, 0x09U, 0x1FU, 0x8DU, 0U, 0x80U});
    } else {
        store(0x8000U, disch ? 0xE5U : 0x1FU);
    }
    store(0x12U, static_cast<Byte>(0xC7U + profile));
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    return code;
}

inline std::vector<core::Byte> make_mapper233_rom(unsigned profile = 0U,
                                                  bool legacy = false,
                                                  bool vertical = true) {
    using core::Byte;
    const std::size_t pages = profile == 1U ? 32U : 64U;
    std::vector<Byte> data(16U + pages * 16384U);
    data[0] = 'N';
    data[1] = 'E';
    data[2] = 'S';
    data[3] = 0x1AU;
    data[4] = static_cast<Byte>(pages);
    data[6] = static_cast<Byte>(0x90U | (vertical ? 1U : 0U));
    data[7] = legacy ? 0xE0U : 0xE8U;
    data[11] = legacy ? 0U : 7U;
    const auto program = mapper233_program(profile);
    for (std::size_t page = 0U; page < pages; ++page) {
        const std::size_t base = 16U + page * 16384U;
        std::fill_n(
            data.begin() + static_cast<std::ptrdiff_t>(base), 16384U, static_cast<Byte>(page));
        std::ranges::copy(program, data.begin() + static_cast<std::ptrdiff_t>(base + 0x2000U));
        for (std::size_t v = base + 0x3FFAU; v < base + 16384U; v += 2U) {
            data[v] = 0U;
            data[v + 1U] = 0xE0U;
        }
    }
    return data;
}

} // namespace mapperbus::tests
