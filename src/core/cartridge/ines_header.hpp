#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "core/types.hpp"

namespace mapperbus::core {

struct INesHeader {
    uint16_t prg_rom_banks; // 16 KB units; zero for exponent/multiplier encoding
    uint16_t chr_rom_banks; // 8 KB units; zero for exponent/multiplier encoding
    uint16_t mapper_number;
    uint8_t submapper;
    MirrorMode mirror_mode;
    Region region;
    bool has_battery;
    bool has_trainer;
    bool is_nes2;
    std::size_t prg_rom_size = 0;
    std::size_t chr_rom_size = 0;
};

Result<INesHeader> parse_ines_header(std::span<const Byte> rom_data);

/// Detect region from ROM filename tags like (U), (USA), (J), (E), (EUR), etc.
/// Returns std::nullopt if no region tag is found.
std::optional<Region> detect_region_from_filename(const std::string& filename);

} // namespace mapperbus::core
