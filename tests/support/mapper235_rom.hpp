#pragma once

#include <algorithm>
#include <bit>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::tests {

/// Original 6502 checks. Every physical 16 KiB page has the same firmware.
inline std::vector<core::Byte> mapper235_program(core::Byte sockets, bool floating = false) {
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
    pwrite(0x123U, 0x5AU);
    pread(0x123U, 0x5AU);
    if (floating) {
        unsigned hole = 1U;
        while ((sockets & (1U << hole)) != 0U)
            ++hole;
        const auto empty = static_cast<Address>(0x8000U + hole * 256U);
        store(empty, 0xFFU);
        expect(0x9A00U, 0x9AU);
        expect(0xFA00U, 0xFAU);
        pwrite(0x123U, 0xA7U);
        pread(0x123U, 0xA7U);
        store(static_cast<Address>(empty + 0x3C00U), 0U);
        pwrite(0x2000U, 0x19U);
        pwrite(0x2C00U, 0x27U);
        pread(0x2400U, 0x27U); // Single lower overrides vertical while ROM is absent.
        store(0x8000U, 0U);
        expect(0x9A00U, 0U);
        expect(0xFA00U, 1U);
        store(empty, 0U);
    } else {
        unsigned packed = 0U;
        for (unsigned socket = 0U; socket < 4U; ++socket) {
            if ((sockets & (1U << socket)) == 0U)
                continue;
            for (unsigned bank : {0U, 7U, 31U})
                for (bool small : {false, true})
                    for (bool upper : {false, true}) {
                        store(static_cast<Address>(0x8000U + socket * 256U + bank +
                                                   (small ? 2048U : 0U) + (upper ? 4096U : 0U)),
                              0xFFU);
                        const auto base = packed * 64U + bank * 2U;
                        expect(0x9A00U, static_cast<Byte>(base + (small && upper ? 1U : 0U)));
                        expect(0xFA00U, static_cast<Byte>(base + (small ? (upper ? 1U : 0U) : 1U)));
                    }
            ++packed;
        }
        store(0xA000U, 0U);
        pwrite(0x2000U, 0x19U);
        pwrite(0x2400U, 0x27U);
        store(0x8000U, 0U);
        pread(0x2400U, 0x19U);
        pread(0x2800U, 0x27U);
        store(0xA000U, 0U);
        pread(0x2400U, 0x27U);
        pread(0x2800U, 0x19U);
        store(0xA400U, 0U);
        for (Address a : {0x2000U, 0x2400U, 0x2800U, 0x2C00U})
            pread(a, 0x19U);
        pwrite(0x2C00U, 0x39U);
        store(0xA000U, 0U);
        pread(0x2000U, 0x39U);
        pread(0x2400U, 0x27U);
        store(0xBC1FU, 0U);
    }
    store(0x12U, floating ? 0xD7U : 0xC7U);
    const auto loop = static_cast<Address>((floating ? 0x400U : 0xE000U) + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    return code;
}

inline std::vector<core::Byte> mapper235_boot(core::Byte sockets, bool floating = false) {
    using core::Byte;
    auto code = mapper235_program(sockets, floating);
    if (!floating)
        return code;
    std::vector<Byte> boot;
    for (std::size_t i = 0U; i < code.size(); ++i) {
        const auto a = static_cast<core::Address>(0x400U + i);
        boot.insert(boot.end(),
                    {0xA9U, code[i], 0x8DU, static_cast<Byte>(a), static_cast<Byte>(a >> 8U)});
    }
    boot.insert(boot.end(), {0x4CU, 0U, 4U});
    return boot;
}

inline std::vector<core::Byte> make_mapper235_rom(core::Byte sockets = 5U,
                                                  bool legacy = false,
                                                  bool vertical = true,
                                                  bool floating = false) {
    using core::Byte;
    const auto pages = static_cast<unsigned>(std::popcount(sockets)) * 64U;
    std::vector<Byte> data(16U + pages * 16384U);
    data[0] = 'N';
    data[1] = 'E';
    data[2] = 'S';
    data[3] = 0x1AU;
    data[4] = static_cast<Byte>(pages);
    data[6] = static_cast<Byte>(0xB0U | (vertical ? 1U : 0U));
    data[7] = legacy ? 0xE0U : 0xE8U;
    if (!legacy) {
        data[9] = static_cast<Byte>(pages >> 8U);
        data[11] = 7U;
    }
    const auto boot = mapper235_boot(sockets, floating);
    for (unsigned page = 0U; page < pages; ++page) {
        const std::size_t base = 16U + page * 16384U;
        std::fill_n(
            data.begin() + static_cast<std::ptrdiff_t>(base), 16384U, static_cast<Byte>(page));
        std::ranges::copy(boot, data.begin() + static_cast<std::ptrdiff_t>(base + 0x2000U));
        for (std::size_t v = base + 0x3FFAU; v < base + 16384U; v += 2U) {
            data[v] = 0U;
            data[v + 1U] = 0xE0U;
        }
    }
    return data;
}

} // namespace mapperbus::tests
