#pragma once

#include <cstring>

#include "core/emulator.hpp"
#include "core/state/state.hpp"

namespace mapperbus::tests {

/// Derive v18-v7 fixtures by removing newer component extensions. FDS
/// v4-v6 conversions can then remove their older tail extensions independently.
inline std::vector<core::Byte> legacy_state(const core::Emulator& emulator, uint32_t version) {
    auto blob = emulator.save_state();
    core::StateWriter cpu, ppu, apu, bus;
    emulator.cpu().save_state(cpu);
    emulator.ppu().save_state(ppu);
    core::Apu{}.save_state(apu);
    core::MemoryBus{}.save_state(bus);
    constexpr auto kHeader = 4 + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(core::Region) + 32;
    const auto cpu_end = kHeader + cpu.data().size();
    const auto ppu_end = cpu_end + ppu.data().size();
    const auto apu_end = ppu_end + apu.data().size();
    const auto bus_end = apu_end + bus.data().size();
    const auto erase = [&blob](std::size_t end, std::size_t count) {
        blob.erase(blob.begin() + static_cast<std::ptrdiff_t>(end - count),
                   blob.begin() + static_cast<std::ptrdiff_t>(end));
    };
    if (version < 19)
        erase(blob.size(), 5); // Hori adapter pads 3/4 appended after existing external state
    if (version < 17)
        erase(blob.size(), 5); // external pad reports appended after cartridge state
    const std::size_t old_bus_tail = version < 9 ? 5 : version < 11 ? 3 : version < 13 ? 2 : 0;
    erase(bus_end,
          old_bus_tail + (version < 14 ? 1 : 0) + (version < 15 ? 1 : 0) + (version < 16 ? 3 : 0) +
              (version < 17 ? 1 : 0) + (version < 18 ? 9 : 0));
    if (version < 12)
        erase(apu_end, version < 9 ? (version < 8 ? 18 : 7) : 6);
    if (version < 13)
        erase(ppu_end, version < 10 ? (version < 9 ? 4 : 3) : 1);
    if (version < 9)
        erase(cpu_end, 1);
    std::memcpy(blob.data() + 4, &version, sizeof(version));
    return blob;
}

} // namespace mapperbus::tests
