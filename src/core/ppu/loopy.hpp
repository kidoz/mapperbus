#pragma once

#include <cstdint>

// Loopy PPU internal scroll register model — field layout, bit masks,
// and helper accessors shared between the PPU implementation and tests.
//
// Both `v` (current VRAM address) and `t` (temporary) are 15-bit values:
//
//     yyy NN YYYYY XXXXX
//     ||| || ||||| +++++-- coarse X scroll (0-31)
//     ||| || +++++-------- coarse Y scroll (0-29 normally; wraps at 31)
//     ||| ++-------------- nametable select (0-3)
//     +++----------------- fine Y scroll (0-7)
//
// See: https://www.nesdev.org/wiki/PPU_scrolling

namespace mapperbus::core::loopy {

inline constexpr std::uint16_t kCoarseXMask = 0x001F;
inline constexpr std::uint16_t kCoarseYMask = 0x03E0;
inline constexpr std::uint16_t kCoarseYShift = 5;
inline constexpr std::uint16_t kNametableMask = 0x0C00;
inline constexpr std::uint16_t kNametableHMask = 0x0400;
inline constexpr std::uint16_t kNametableVMask = 0x0800;
inline constexpr std::uint16_t kFineYMask = 0x7000;
inline constexpr std::uint16_t kFineYShift = 12;

// Bits copied from `t` to `v` at PPU cycle 257 of each visible scanline.
inline constexpr std::uint16_t kHorizontalMask = kCoarseXMask | kNametableHMask;
// Bits copied from `t` to `v` during cycles 280-304 of the pre-render line.
inline constexpr std::uint16_t kVerticalMask = kCoarseYMask | kNametableVMask | kFineYMask;

// Maximum legal v value (15-bit register).
inline constexpr std::uint16_t kVAddressMask = 0x7FFF;

[[nodiscard]] inline constexpr std::uint16_t coarse_x(std::uint16_t v) noexcept {
    return v & kCoarseXMask;
}

[[nodiscard]] inline constexpr std::uint16_t coarse_y(std::uint16_t v) noexcept {
    return (v & kCoarseYMask) >> kCoarseYShift;
}

[[nodiscard]] inline constexpr std::uint16_t fine_y(std::uint16_t v) noexcept {
    return (v & kFineYMask) >> kFineYShift;
}

[[nodiscard]] inline constexpr std::uint16_t nametable(std::uint16_t v) noexcept {
    return (v & kNametableMask) >> 10;
}

// Tile (nametable byte) address for the current v. Per the wiki formula:
//     0x2000 | (v & 0x0FFF)
[[nodiscard]] inline constexpr std::uint16_t tile_address(std::uint16_t v) noexcept {
    return static_cast<std::uint16_t>(0x2000 | (v & 0x0FFF));
}

// Attribute byte address for the current v. Per the wiki formula:
//     0x23C0 | (v & 0x0C00) | ((v >> 4) & 0x38) | ((v >> 2) & 0x07)
[[nodiscard]] inline constexpr std::uint16_t attribute_address(std::uint16_t v) noexcept {
    return static_cast<std::uint16_t>(0x23C0 | (v & kNametableMask) | ((v >> 4) & 0x38) |
                                      ((v >> 2) & 0x07));
}

} // namespace mapperbus::core::loopy
