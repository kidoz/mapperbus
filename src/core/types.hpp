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
    Dendy, // Hybrid (UA6527P/UA6538); legacy identifier, serialized value 2
    Multi, // Multiple-region (game self-detects)
};

/// NTSC controller output-enable wiring, independent of CPU revision and region.
enum class ControllerTiming : std::uint8_t {
    NesAvFamicom,
    RfFamicom,
};

/// Controller data pins and built-in pad buttons, separate from clock timing.
enum class ControllerWiring : std::uint8_t {
    Nes,
    RfFamicom,
};

enum class ControllerMode : std::uint8_t {
    Standard,
    FourScore,
    HoriFourPlayer,
};

struct RegionTiming {
    int scanlines_per_frame;
    int vblank_scanline;
    int pre_render_scanline;
    bool odd_frame_skip = false;
    unsigned cpu_clock_divider = 12;
    unsigned ppu_clock_divider = 4;
    // Selected divider alignment: retain the NTSC two-dot/one-dot bus split.
    unsigned cpu_access_master_clock = 8;
    double master_clock_hz = 236'250'000.0 / 11.0;
    bool swap_color_emphasis = false;

    [[nodiscard]] constexpr double cpu_clock_hz() const {
        return master_clock_hz / cpu_clock_divider;
    }

    /// Nominal host pacing; rendered NTSC frames average one skipped dot per pair.
    [[nodiscard]] constexpr long frame_duration_ns() const {
        const double dots = scanlines_per_frame * 341.0 - (odd_frame_skip ? 0.5 : 0.0);
        return static_cast<long>(dots * ppu_clock_divider * 1'000'000'000.0 / master_clock_hz +
                                 0.5);
    }
};

inline constexpr RegionTiming kNtscTiming = {.scanlines_per_frame = 262,
                                             .vblank_scanline = 241,
                                             .pre_render_scanline = 261,
                                             .odd_frame_skip = true};
inline constexpr RegionTiming kPalTiming = {.scanlines_per_frame = 312,
                                            .vblank_scanline = 241,
                                            .pre_render_scanline = 311,
                                            .cpu_clock_divider = 16,
                                            .ppu_clock_divider = 5,
                                            .master_clock_hz = 26'601'712.5,
                                            .swap_color_emphasis = true};
inline constexpr RegionTiming kDendyTiming = {.scanlines_per_frame = 312,
                                              .vblank_scanline = 291,
                                              .pre_render_scanline = 311,
                                              .cpu_clock_divider = 15,
                                              .ppu_clock_divider = 5,
                                              .cpu_access_master_clock = 10,
                                              .master_clock_hz = 26'601'712.5,
                                              .swap_color_emphasis = true};

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
