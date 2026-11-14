#pragma once

#include <algorithm>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::tests {

/// Original 6502 fixtures, with separate firmware on the two physical PRG chips.
inline std::vector<core::Byte> mapper230_program(bool multicart) {
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
    paddr(0x123U);
    code.insert(code.end(), {0xADU, 7U, 0x20U, 0xADU, 7U, 0x20U, 0x85U, 0x14U});
    pwrite(0x123U, multicart ? 0xA7U : 0x39U);
    pread(0x123U, multicart ? 0xA7U : 0x39U);
    if (multicart) {
        for (unsigned page = 0U; page < 32U; ++page)
            for (bool small : {false, true})
                for (bool vertical : {false, true}) {
                    const auto value =
                        static_cast<Byte>(page + (small ? 32U : 0U) + (vertical ? 64U : 0U));
                    store(0x8000U, value);
                    const unsigned low = small ? page : page / 2U * 2U;
                    const unsigned high = small ? page : page / 2U * 2U + 1U;
                    expect(0x9A00U, static_cast<Byte>(8U + low));
                    expect(0xFA00U, static_cast<Byte>(8U + high));
                }
    } else {
        for (Byte value : {0U, 1U, 5U, 7U, 0x40U, 0xFFU}) {
            store(0x8000U, value);
            expect(0x9A00U, value % 8U);
            expect(0xFA00U, 7U);
        }
    }
    store(0x8000U, 0x40U); // Vertical in both modes.
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    store(0x8000U, 0U);
    pread(0x2400U, multicart ? 0x19U : 0x27U);
    store(0x8000U, 0x40U);
    pread(0x2400U, 0x27U);
    store(0x8000U, multicart ? 0x7FU : 5U);
    store(0x12U, multicart ? 0xD7U : 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    return code;
}

inline std::vector<core::Byte> make_mapper230_rom(bool legacy = false, bool vertical = true) {
    using core::Byte;
    std::vector<Byte> data(16U + 655360U);
    data[0] = 'N';
    data[1] = 'E';
    data[2] = 'S';
    data[3] = 0x1AU;
    data[4] = 40U;
    data[6] = static_cast<Byte>(0x60U | (vertical ? 1U : 0U));
    data[7] = legacy ? 0xE0U : 0xE8U;
    data[11] = legacy ? 0U : 7U;
    const auto contra = mapper230_program(false);
    const auto multicart = mapper230_program(true);
    for (std::size_t page = 0U; page < 40U; ++page) {
        const std::size_t base = 16U + page * 16384U;
        std::fill_n(
            data.begin() + static_cast<std::ptrdiff_t>(base), 16384U, static_cast<Byte>(page));
        const auto& program = page < 8U ? contra : multicart;
        std::ranges::copy(program, data.begin() + static_cast<std::ptrdiff_t>(base + 0x2000U));
        for (std::size_t v = base + 0x3FFAU; v < base + 16384U; v += 2U) {
            data[v] = 0U;
            data[v + 1U] = 0xE0U;
        }
    }
    return data;
}

} // namespace mapperbus::tests
