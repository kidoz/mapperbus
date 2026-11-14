#pragma once

#include <algorithm>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::tests {
/// Original firmware replicated into each 32 KiB page so switching remains executable.
inline std::vector<core::Byte> make_mapper241_rom(bool legacy = false,
                                                  bool vertical = true,
                                                  bool battery = false,
                                                  bool chr_rom = false,
                                                  std::size_t prg_size = 131072U) {
    using core::Address;
    using core::Byte;
    std::vector<Byte> bytes(16U + prg_size + (chr_rom ? 8192U : 0U), 0U);
    bytes[0] = 'N';
    bytes[1] = 'E';
    bytes[2] = 'S';
    bytes[3] = 0x1AU;
    bytes[4] = static_cast<Byte>(prg_size / 16384U);
    bytes[5] = chr_rom ? 1U : 0U;
    bytes[6] = static_cast<Byte>(0x10U | (vertical ? 1U : 0U) | (battery ? 2U : 0U));
    bytes[7] = legacy ? 0xF0U : 0xF8U;
    if (!legacy) {
        bytes[9] = static_cast<Byte>((prg_size / 16384U) >> 8U);
        bytes[10] = battery ? 0x70U : 7U;
        bytes[11] = chr_rom ? 0U : 7U;
    }
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
    code.insert(code.end(), {0xADU, 0U, 0x60U, 0x85U, 0x14U});
    paddr(0U);
    code.insert(code.end(), {0xADU, 7U, 0x20U, 0xADU, 7U, 0x20U, 0x85U, 0x15U});
    store(0x6000U, 0xA7U);
    for (Address addr : {0x7FFDU, 0x7FFEU, 0x7FFFU}) {
        store(addr, 0x5AU);
        expect(addr, 0x5AU);
    }
    const auto mask = static_cast<unsigned>(prg_size / 32768U - 1U);
    for (Byte data : {0U, 1U, 3U, 0x80U, 0xFFU}) {
        store(0xFFFFU, data);
        expect(0x9A00U, static_cast<Byte>(data & mask));
        expect(0xFA00U, static_cast<Byte>(data & mask));
        expect(0x6000U, 0xA7U);
        pwrite(0U, 0x5AU);
        pread(0U, chr_rom ? 0x23U : 0x5AU);
    }
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    pread(0x2000U, vertical ? 0x19U : 0x27U);
    pread(0x2800U, vertical ? 0x19U : 0U);
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    for (std::size_t page = 0U; page < prg_size / 32768U; ++page) {
        const auto base = 16U + page * 32768U;
        std::fill_n(
            bytes.begin() + static_cast<std::ptrdiff_t>(base), 32768U, static_cast<Byte>(page));
        std::ranges::copy(code, bytes.begin() + static_cast<std::ptrdiff_t>(base + 0x6000U));
        for (std::size_t vector = 0x7FFAU; vector < 0x8000U; vector += 2U) {
            bytes[base + vector] = 0U;
            bytes[base + vector + 1U] = 0xE0U;
        }
    }
    if (chr_rom)
        std::fill_n(bytes.begin() + static_cast<std::ptrdiff_t>(16U + prg_size), 8192U, 0x23U);
    return bytes;
}
} // namespace mapperbus::tests
