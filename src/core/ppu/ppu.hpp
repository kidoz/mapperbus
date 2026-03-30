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
    Byte read_buffer_ = 0;
    bool nmi_pending_ = false;

    // Loopy internal registers — pure model per Mesen2/FCEUX/NESDev wiki.
    //
    // v and t are 15-bit registers:
    //   yyy NN YYYYY XXXXX
    //   ||| || ||||| +++++-- coarse X scroll (0-31)
    //   ||| || +++++-------- coarse Y scroll (0-29)
    //   ||| ++-------------- nametable select (0-3)
    //   +++----------------- fine Y scroll (0-7)
    //
    // $2005 and $2006 both write to t through the shared w toggle.
    // $2006 second write copies t → v immediately.
    // During rendering, v is used for tile fetches and updated per scanline.
    uint16_t reg_v_ = 0; // Current VRAM address (used for rendering and $2007 access)
    uint16_t reg_t_ = 0; // Temporary VRAM address (staging for $2005/$2006)
    uint8_t fine_x_ = 0; // Fine X scroll (3 bits, from $2005 first write)
    bool write_latch_ = false; // Shared w toggle for $2005/$2006, reset by $2002 read

    const RegionTiming* timing_ = &kNtscTiming;

    bool rendering_enabled() const {
        return (ppumask_ & 0x18) != 0;
    }

private:

    Byte read_vram(Address addr) const;
    void write_vram(Address addr, Byte value);
    Address mirror_nametable_address(Address addr) const;
    Byte read_palette(Address addr) const;
    void write_palette(Address addr, Byte value);
    std::uint32_t palette_color(Byte palette_value) const;

    void increment_coarse_x();
    void increment_fine_y();
    void copy_horizontal_from_t();
    void copy_vertical_from_t();

    void render_scanline();
    void render_background_scanline(int y, std::array<bool, kScreenWidth>& bg_opaque);
    void render_sprites_scanline(int y, const std::array<bool, kScreenWidth>& bg_opaque);
};

} // namespace mapperbus::core
