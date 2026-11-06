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

/// NTSC controller output-enable wiring, independent of CPU revision and region.
enum class ControllerTiming : std::uint8_t {
    NesAvFamicom,
    RfFamicom,
};

struct RegionTiming {
    int scanlines_per_frame;
    int vblank_scanline;
    int pre_render_scanline;
    long frame_duration_ns; // nanoseconds per frame
    bool odd_frame_skip = false;
    unsigned cpu_clock_divider = 12;
    unsigned ppu_clock_divider = 4;
    // Selected divider alignment: retain the NTSC two-dot/one-dot bus split.
    unsigned cpu_access_master_clock = 8;
};

inline constexpr RegionTiming kNtscTiming = {.scanlines_per_frame = 262,
                                             .vblank_scanline = 241,
                                             .pre_render_scanline = 261,
                                             .frame_duration_ns = 16'639'267,
                                             .odd_frame_skip = true};
inline constexpr RegionTiming kPalTiming = {.scanlines_per_frame = 312,
                                            .vblank_scanline = 241,
                                            .pre_render_scanline = 311,
                                            .frame_duration_ns = 19'997'200,
                                            .cpu_clock_divider = 16,
                                            .ppu_clock_divider = 5};
inline constexpr RegionTiming kDendyTiming = {.scanlines_per_frame = 312,
                                              .vblank_scanline = 291,
                                              .pre_render_scanline = 311,
                                              .frame_duration_ns = 20'000'000,
                                              .cpu_clock_divider = 15,
                                              .ppu_clock_divider = 5,
                                              .cpu_access_master_clock = 10};

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
