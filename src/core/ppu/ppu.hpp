#pragma once

#include <array>
#include <cstdint>

#include "core/types.hpp"

namespace mapperbus::core {

class Cartridge;

class Ppu {
  public:
    Ppu() = default;

    void set_region(Region region);
    void reset();
    void step(uint32_t cpu_cycles);

    bool frame_ready() const {
        return frame_ready_;
    }
    void clear_frame_ready() {
        frame_ready_ = false;
    }
    const FrameBuffer& frame_buffer() const {
        return frame_buffer_;
    }
    const std::array<Byte, kVRamSize>& vram() const {
        return vram_;
    }
    const std::array<Byte, 32>& palette_ram() const {
        return palette_;
    }
    Byte ppuctrl() const {
        return ppuctrl_;
    }
    Byte ppumask() const {
        return ppumask_;
    }

    Byte read_register(uint8_t reg);
    void write_register(uint8_t reg, Byte value);
    bool consume_nmi();
    void write_oam_dma(Byte value);

    void connect_cartridge(Cartridge* cartridge) {
        cartridge_ = cartridge;
    }

  private:
    // clang-format off
    static constexpr std::array<std::uint32_t, 64> kNesPalette = {
        0xFF666666, 0xFF002A88, 0xFF1412A7, 0xFF3B00A4, 0xFF5C007E, 0xFF6E0040, 0xFF6C0600,
        0xFF561D00, 0xFF333500, 0xFF0B4800, 0xFF005200, 0xFF004F08, 0xFF00404D, 0xFF000000,
        0xFF000000, 0xFF000000, 0xFFADADAD, 0xFF155FD9, 0xFF4240FF, 0xFF7527FE, 0xFFA01ACC,
        0xFFB71E7B, 0xFFB53120, 0xFF994E00, 0xFF6B6D00, 0xFF388700, 0xFF0C9300, 0xFF008F32,
        0xFF007C8D, 0xFF000000, 0xFF000000, 0xFF000000, 0xFFFFFEFF, 0xFF64B0FF, 0xFF9290FF,
        0xFFC676FF, 0xFFF36AFF, 0xFFFE6ECC, 0xFFFE8170, 0xFFEA9E22, 0xFFBCBE00, 0xFF88D800,
        0xFF5CE430, 0xFF45E082, 0xFF48CDDE, 0xFF4F4F4F, 0xFF000000, 0xFF000000, 0xFFFFFEFF,
        0xFFC0DFFF, 0xFFD3D2FF, 0xFFE8C8FF, 0xFFFBC2FF, 0xFFFEC4EA, 0xFFFECCC5, 0xFFF7D8A5,
        0xFFE4E594, 0xFFCFEF96, 0xFFBDF4AB, 0xFFB3F3CC, 0xFFB5EBF2, 0xFFB8B8B8, 0xFF000000,
        0xFF000000,
    };
    // clang-format on

    FrameBuffer frame_buffer_{};
    bool frame_ready_ = false;
    Cartridge* cartridge_ = nullptr;

    uint16_t scanline_ = 0;
    uint16_t cycle_ = 0;
    std::array<Byte, 256> oam_{};
    std::array<Byte, kVRamSize> vram_{};
    std::array<Byte, 32> palette_{};

    Byte ppuctrl_ = 0;
    Byte ppumask_ = 0;
    Byte ppustatus_ = 0;
    Byte oam_addr_ = 0;
    Address ppu_addr_ = 0;
    Byte read_buffer_ = 0;
    Byte scroll_x_ = 0;
    Byte scroll_y_ = 0;
    bool address_latch_ = false;
    bool nmi_pending_ = false;

    const RegionTiming* timing_ = &kNtscTiming;

    Byte read_vram(Address addr) const;
    void write_vram(Address addr, Byte value);
    Address mirror_nametable_address(Address addr) const;
    Byte read_palette(Address addr) const;
    void write_palette(Address addr, Byte value);
    std::uint32_t palette_color(Byte palette_value) const;

    void render_scanline();
    void render_background_scanline(int y, std::array<bool, kScreenWidth>& bg_opaque);
    void render_sprites_scanline(int y, const std::array<bool, kScreenWidth>& bg_opaque);
};

} // namespace mapperbus::core
