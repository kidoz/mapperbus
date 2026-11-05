#pragma once

#include <array>
#include <span>
#include <string>

#include "core/types.hpp"

namespace mapperbus::core {

using Sha256Digest = std::array<Byte, 32>;

/// SHA-256 of the exact image bytes, independent of filename and host byte order.
[[nodiscard]] Sha256Digest rom_sha256(std::span<const Byte> data);
[[nodiscard]] std::string sha256_hex(const Sha256Digest& digest);

} // namespace mapperbus::core
