#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::tests {
/// Pin-level oracle: compose the ROM address one documented signal at a time.
inline unsigned mapper242_offset(
    core::Address cpu, core::Address latch, bool battery, bool et, core::Byte pads) {
    const auto pin = [latch](unsigned bit) { return (latch >> bit) & 1U; };
    unsigned a14 = pin(0U) ? ((cpu >> 14U) & 1U) : pin(2U);
    unsigned a15 = pin(3U), a16 = pin(4U);
    if (cpu >= 0xC000U && !battery && pin(7U) == 0U)
        a14 = a15 = a16 = pin(9U);
    unsigned offset = (cpu & 0x3FFFU) | (a14 << 14U) | (a15 << 15U) | (a16 << 16U);
    if (et && pin(10U) == 0U)
        offset += 524288U;
    else
        offset |= (pin(5U) << 17U) | (pin(6U) << 18U);
    if (pin(8U) != 0U)
        offset = (offset & ~31U) | pads;
    return offset;
}
inline core::Byte mapper242_byte(unsigned offset) {
    return static_cast<core::Byte>((offset / 16384U) * 37U + (offset & 31U));
}
/// Original RAM-resident firmware; address/pad substitution cannot corrupt its opcodes.
inline std::vector<core::Byte> make_mapper242_rom(bool legacy = false,
                                                  bool battery = false,
                                                  bool et = false,
                                                  core::Byte pads = 0U,
                                                  bool vertical = true) {
    using core::Address;
    using core::Byte;
    const unsigned prg_size = et ? 655360U : 524288U;
    std::vector<Byte> bytes(16U + prg_size);
    bytes[0] = 'N';
    bytes[1] = 'E';
    bytes[2] = 'S';
    bytes[3] = 0x1AU;
    bytes[4] = static_cast<Byte>(prg_size / 16384U);
    bytes[5] = 0U;
    bytes[6] = static_cast<Byte>(0x20U | (battery ? 2U : 0U) | (vertical ? 1U : 0U));
    bytes[7] = legacy ? 0xF0U : 0xF8U;
    if (!legacy) {
        bytes[10] = battery ? 0x70U : 0U;
        bytes[11] = 7U;
    }
    for (unsigned i = 0; i < prg_size; ++i)
        bytes[16U + i] = mapper242_byte(i);
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
    store(0x12U, 0U); // A reset must complete the checks again, not reuse an old signature.
    store(0x13U, 0U);
    code.insert(code.end(), {0xADU, 0U, 0x60U, 0x85U, 0x15U});
    paddr(0U);
    code.insert(code.end(), {0xADU, 7U, 0x20U, 0xADU, 7U, 0x20U, 0x85U, 0x14U});
    for (Address latch : {0x000U, 0x001U, 0x201U, 0x078U, 0x279U, 0x49CU, 0x4BDU, 0x7EFU}) {
        store(static_cast<Address>(0x8000U | latch), 0U);
        for (Address cpu : {0x9A07U, 0xFA07U})
            expect(cpu, mapper242_byte(mapper242_offset(cpu, latch, battery, et, pads)));
    }
    store(0x8000U, 0U);
    pwrite(0U, 0x5AU);
    pread(0U, 0x5AU);
    store(0x8080U, 0U);
    pwrite(0U, 0xA6U);
    pread(0U, battery ? 0xA6U : 0x5AU);
    store(0x8000U, 0U);
    pwrite(0U, 0x7BU);
    pread(0U, 0x7BU);
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    pread(0x2800U, 0x19U);
    store(0x8002U, 0U);
    pread(0x2400U, 0x19U);
    pread(0x2800U, 0x27U);
    if (battery) {
        store(0x6000U, 0xA7U);
        store(0x7FFFU, 0x91U);
        expect(0x7FFFU, 0x91U);
    }
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0x0200U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    assert(code.size() < 1024U);
    std::vector<Byte> boot;
    for (Byte page = 0U; page < 4U; ++page)
        boot.insert(boot.end(),
                    {0xA2U,
                     0U,
                     0xBDU,
                     0U,
                     static_cast<Byte>(0xE1U + page),
                     0x9DU,
                     0U,
                     static_cast<Byte>(2U + page),
                     0xE8U,
                     0xD0U,
                     0xF7U});
    boot.insert(boot.end(), {0x4CU, 0U, 2U});
    for (unsigned bank = 0U; bank < prg_size / 16384U; ++bank) {
        const auto base = 16U + bank * 16384U;
        std::ranges::copy(boot, bytes.begin() + static_cast<std::ptrdiff_t>(base + 0x2000U));
        std::ranges::copy(code, bytes.begin() + static_cast<std::ptrdiff_t>(base + 0x2100U));
        for (unsigned v = 0x3FFAU; v < 0x4000U; v += 2U) {
            bytes[base + v] = 0U;
            bytes[base + v + 1U] = 0xE0U;
        }
    }
    return bytes;
}
} // namespace mapperbus::tests
