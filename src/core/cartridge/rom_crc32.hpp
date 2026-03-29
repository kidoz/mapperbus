#pragma once

#include <cstdint>
#include <span>

#include "core/types.hpp"

namespace mapperbus::core {

/// Compute CRC32 (ISO 3309 / ITU-T V.42) of arbitrary data.
uint32_t crc32(std::span<const Byte> data);

/// Compute CRC32 of ROM data, skipping the 16-byte iNES header.
/// This is the community-standard hash used by NesCartDB and No-Intro.
/// Returns 0 if the data is too small to contain a header.
uint32_t rom_crc32(std::span<const Byte> rom_data);

} // namespace mapperbus::core
