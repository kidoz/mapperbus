#pragma once

#include <algorithm>
#include <array>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::tests {
inline constexpr std::array<std::size_t, 3> kRealtecPrgSizes{131072U, 262144U, 524288U};
inline constexpr std::array<std::size_t, 3> kRealtecChrSizes{65536U, 131072U, 0U};
inline core::Byte mapper236_marker(unsigned bank, unsigned offset) {
    return static_cast<core::Byte>(bank * 7U + offset / 16U * 3U + offset % 16U);
}

/// Original firmware executes from $0200: pad mode also rewires opcode fetches.
inline std::vector<core::Byte> mapper236_program(unsigned population, core::Byte pads = 0U) {
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
    const bool ram = population == 2U;
    const unsigned pages = static_cast<unsigned>(kRealtecPrgSizes[population] / 16384U);
    store(0x4017U, 0x40U);
    store(0x12U, 0U);
    store(0x13U, 0U);
    paddr(0x123U);
    code.insert(code.end(), {0xADU, 7U, 0x20U, 0xADU, 7U, 0x20U, 0x85U, 0x14U});
    pwrite(0x123U, 0x5AU);
    for (unsigned lower : {0U, 3U, 15U}) {
        store(static_cast<Address>(0x8000U + lower), 0xFFU);
        pread(0x123U,
              ram ? 0x5AU : static_cast<Byte>(lower % (kRealtecChrSizes[population] / 8192U)));
        if (ram) {
            store(0xC000U, 0xFFU);
            const unsigned block = (lower % 8U) * 8U % pages;
            expect(0x9A80U, mapper236_marker(block, 0x1A80U));
            expect(0xFA80U, mapper236_marker(block + 7U, 0x3A80U));
        }
    }
    const unsigned block = ram ? 24U : (population == 1U ? 8U : 0U);
    for (unsigned mode : {0U, 2U, 3U})
        for (unsigned bank : {0U, 3U, 7U}) {
            store(static_cast<Address>(0xC000U + mode * 16U + (ram ? 0U : 8U) + bank), 0xFFU);
            const unsigned lo = mode == 2U ? bank / 2U * 2U : bank;
            const unsigned hi = mode == 0U ? 7U : (mode == 2U ? lo + 1U : bank);
            expect(0x9A80U, mapper236_marker(block + lo, 0x1A80U));
            expect(0xFA80U, mapper236_marker(block + hi, 0x3A80U));
        }
    store(ram ? 0xC013U : 0xC01BU, 0U);
    for (unsigned nibble = 0U; nibble < 16U; ++nibble) {
        expect(static_cast<Address>(0x9A80U + nibble),
               mapper236_marker(block + 3U, 0x1A80U + pads));
        expect(static_cast<Address>(0xFA80U + nibble),
               mapper236_marker(block + 7U, 0x3A80U + pads));
    }
    store(0xC000U, 0U);
    store(0x8000U, 0U);
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    store(0x8020U, 0U);
    pread(0x2400U, 0x19U);
    pread(0x2800U, 0x27U);
    store(0x8000U, 0U);
    pread(0x2400U, 0x27U);
    pread(0x2800U, 0x19U);
    store(ram ? 0x8027U : 0x802FU, 0xFFU);
    store(static_cast<Address>((ram ? 0xC007U : 0xC00FU) + ((pads & 1U) != 0U ? 0x10U : 0x30U)),
          0xFFU);
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0x200U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    return code;
}
inline std::vector<core::Byte> mapper236_boot(unsigned population, core::Byte pads = 0U) {
    using core::Byte;
    const auto code = mapper236_program(population, pads);
    std::vector<Byte> boot;
    for (std::size_t i = 0U; i < code.size(); ++i) {
        const auto a = static_cast<core::Address>(0x200U + i);
        boot.insert(boot.end(),
                    {0xA9U, code[i], 0x8DU, static_cast<Byte>(a), static_cast<Byte>(a >> 8U)});
    }
    boot.insert(boot.end(), {0x4CU, 0U, 2U});
    return boot;
}
inline std::vector<core::Byte> make_mapper236_rom(unsigned population = 0U,
                                                  core::Byte pads = 0U,
                                                  bool legacy = false,
                                                  bool vertical = true) {
    using core::Byte;
    const auto prg_size = kRealtecPrgSizes[population], chr_size = kRealtecChrSizes[population];
    std::vector<Byte> bytes(16U + prg_size + chr_size);
    bytes[0] = 'N';
    bytes[1] = 'E';
    bytes[2] = 'S';
    bytes[3] = 0x1AU;
    bytes[4] = static_cast<Byte>(prg_size / 16384U);
    bytes[5] = static_cast<Byte>(chr_size / 8192U);
    bytes[6] = static_cast<Byte>(0xC0U | (vertical ? 1U : 0U));
    bytes[7] = legacy ? 0xE0U : 0xE8U;
    if (!legacy && population == 2U)
        bytes[11] = 7U;
    const auto boot = mapper236_boot(population, pads);
    for (unsigned page = 0U; page < prg_size / 16384U; ++page) {
        const std::size_t base = 16U + page * 16384U;
        for (unsigned offset = 0U; offset < 16384U; ++offset)
            bytes[base + offset] = mapper236_marker(page, offset);
        std::ranges::copy(boot, bytes.begin() + static_cast<std::ptrdiff_t>(base + 0x2000U));
        for (std::size_t v = base + 0x3FFAU; v < base + 16384U; v += 2U) {
            bytes[v] = 0U;
            bytes[v + 1U] = 0xE0U;
        }
    }
    for (unsigned page = 0U; page < chr_size / 8192U; ++page)
        std::fill_n(bytes.begin() + static_cast<std::ptrdiff_t>(16U + prg_size + page * 8192U),
                    8192U,
                    static_cast<Byte>(page));
    return bytes;
}
} // namespace mapperbus::tests
