#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "core/types.hpp"

namespace mapperbus::core {

struct INesHeader {
    uint8_t prg_rom_banks; // 16 KB units
    uint8_t chr_rom_banks; // 8 KB units
    uint16_t mapper_number;
    MirrorMode mirror_mode;
    Region region;
    bool has_battery;
    bool has_trainer;
    bool is_nes2;
};

Result<INesHeader> parse_ines_header(std::span<const Byte> rom_data);

/// Detect region from ROM filename tags like (U), (USA), (J), (E), (EUR), etc.
/// Returns std::nullopt if no region tag is found.
std::optional<Region> detect_region_from_filename(const std::string& filename);

} // namespace mapperbus::core
