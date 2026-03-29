#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <string>

namespace mapperbus::core {

using Byte = std::uint8_t;
using Word = std::uint16_t;
using Address = std::uint16_t;

template <typename T> using Result = std::expected<T, std::string>;

inline constexpr int kScreenWidth = 256;
inline constexpr int kScreenHeight = 240;
inline constexpr std::size_t kRamSize = 0x0800;  // 2 KB internal RAM
inline constexpr std::size_t kVRamSize = 0x1000; // 4 KB video RAM (supports four-screen mirroring)

struct FrameBuffer {
    std::array<std::uint32_t, kScreenWidth * kScreenHeight> pixels{};
};

enum class MirrorMode : std::uint8_t {
    Horizontal,
    Vertical,
    FourScreen,
    SingleLower,
    SingleUpper,
};

enum class Region : std::uint8_t {
    NTSC,  // RP2C02 — 262 scanlines, 60.0988 Hz
    PAL,   // RP2C07 — 312 scanlines, 50.007 Hz
    Dendy, // UA6538 — 312 scanlines, 50 Hz (NTSC-like CPU speed)
    Multi, // Multiple-region (game self-detects)
};

struct RegionTiming {
    int scanlines_per_frame;
    int vblank_scanline;
    int pre_render_scanline;
    long frame_duration_ns; // nanoseconds per frame
};

inline constexpr RegionTiming kNtscTiming = {262, 241, 261, 16'639'267};
inline constexpr RegionTiming kPalTiming = {312, 241, 311, 19'997'200};
inline constexpr RegionTiming kDendyTiming = {312, 291, 311, 20'000'000};

inline constexpr const RegionTiming& timing_for_region(Region region) {
    switch (region) {
    case Region::PAL:
        return kPalTiming;
    case Region::Dendy:
        return kDendyTiming;
    default:
        return kNtscTiming;
    }
}

} // namespace mapperbus::core
