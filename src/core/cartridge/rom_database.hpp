#pragma once

#include <cstdint>
#include <optional>

#include "core/types.hpp"

namespace mapperbus::core {

struct RomDatabaseEntry {
    uint32_t crc32;
    Region region;
};

/// Look up a ROM's region by CRC32 hash (of headerless data).
/// Returns std::nullopt if the CRC32 is not in the database.
std::optional<Region> lookup_region_by_crc32(uint32_t crc32);

} // namespace mapperbus::core
