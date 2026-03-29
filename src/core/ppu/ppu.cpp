#include "core/ppu/ppu.hpp"

#include <algorithm>

#include "core/cartridge/cartridge.hpp"

namespace mapperbus::core {

void Ppu::set_region(Region region) {
    timing_ = &timing_for_region(region);
}

void Ppu::reset() {
    scanline_ = 0;
    cycle_ = 0;
    frame_ready_ = false;
    ppuctrl_ = 0;
    ppumask_ = 0;
    ppustatus_ = 0;
    oam_addr_ = 0;
    ppu_addr_ = 0;
    read_buffer_ = 0;
    scroll_x_ = 0;
    scroll_y_ = 0;
    address_latch_ = false;
    nmi_pending_ = false;
    oam_.fill(0);
    vram_.fill(0);
    palette_.fill(0);
    frame_buffer_.pixels.fill(0xFF000000);
}

void Ppu::step(uint32_t cpu_cycles) {
    uint32_t ppu_dots = cpu_cycles * 3;
    for (uint32_t i = 0; i < ppu_dots; ++i) {
        ++cycle_;

        // Visible scanlines: render at the end of the visible dot range
        if (scanline_ < 240 && cycle_ == 257) {
            render_scanline();
        }

        // VBlank start
        if (scanline_ == 241 && cycle_ == 1) {
            ppustatus_ |= 0x80;
            frame_ready_ = true;
            if ((ppuctrl_ & 0x80) != 0) {
                nmi_pending_ = true;
            }
        }

        // Pre-render scanline: clear flags
        if (scanline_ == timing_->pre_render_scanline && cycle_ == 1) {
            ppustatus_ &= ~0xE0; // Clear VBlank, sprite 0 hit, sprite overflow
            frame_ready_ = false;
        }

        // Clock mapper IRQ counter on A12 rising edge (sprite fetch, cycle 260)
        if (cycle_ == 260 && (ppumask_ & 0x18) != 0 && cartridge_) {
            if (scanline_ < 240 || scanline_ == timing_->pre_render_scanline) {
                cartridge_->clock_irq_counter();
            }
        }

        if (cycle_ >= 341) {
            cycle_ = 0;
            ++scanline_;
            if (scanline_ >= timing_->scanlines_per_frame) {
                scanline_ = 0;
            }
        }
    }
}

Byte Ppu::read_register(uint8_t reg) {
    reg &= 0x07;
    switch (reg) {
    case 0x02: {
        Byte value = static_cast<Byte>((ppustatus_ & 0xE0) | (read_buffer_ & 0x1F));
        ppustatus_ &= ~0x80;
        address_latch_ = false;
        nmi_pending_ = false;
        return value;
    }
    case 0x04:
        return oam_[oam_addr_];
    case 0x07: {
        Byte value = read_vram(ppu_addr_);
        Byte result = 0;
        if ((ppu_addr_ & 0x3FFF) >= 0x3F00) {
            result = value;
            read_buffer_ = read_vram(static_cast<Address>(ppu_addr_ - 0x1000));
        } else {
            result = read_buffer_;
            read_buffer_ = value;
        }
        ppu_addr_ = static_cast<Address>((ppu_addr_ + ((ppuctrl_ & 0x04) ? 32 : 1)) & 0x3FFF);
        return result;
    }
    default:
        break;
    }
    return 0;
}

void Ppu::write_register(uint8_t reg, Byte value) {
    reg &= 0x07;
    switch (reg) {
    case 0x00:
        ppuctrl_ = value;
        if ((ppuctrl_ & 0x80) != 0 && (ppustatus_ & 0x80) != 0) {
            nmi_pending_ = true;
        }
        break;
    case 0x01:
        ppumask_ = value;
        break;
    case 0x03:
        oam_addr_ = value;
        break;
    case 0x04:
        oam_[oam_addr_++] = value;
        break;
    case 0x05:
        if (!address_latch_) {
            scroll_x_ = value;
        } else {
            scroll_y_ = value;
        }
        address_latch_ = !address_latch_;
        break;
    case 0x06:
        if (!address_latch_) {
            ppu_addr_ = static_cast<Address>((value & 0x3F) << 8);
        } else {
            ppu_addr_ = static_cast<Address>((ppu_addr_ & 0x3F00) | value);
        }
        address_latch_ = !address_latch_;
        break;
    case 0x07:
        write_vram(ppu_addr_, value);
        ppu_addr_ = static_cast<Address>((ppu_addr_ + ((ppuctrl_ & 0x04) ? 32 : 1)) & 0x3FFF);
        break;
    default:
        break;
    }
}

bool Ppu::consume_nmi() {
    bool pending = nmi_pending_;
    nmi_pending_ = false;
    return pending;
}

void Ppu::write_oam_dma(Byte value) {
    oam_[oam_addr_++] = value;
}

Byte Ppu::read_vram(Address addr) const {
    addr &= 0x3FFF;
    if (addr < 0x2000) {
        return cartridge_ ? cartridge_->read_chr(addr) : 0;
    }
    if (addr < 0x3F00) {
        return vram_[mirror_nametable_address(addr)];
    }
    return read_palette(addr);
}

void Ppu::write_vram(Address addr, Byte value) {
    addr &= 0x3FFF;
    if (addr < 0x2000) {
        if (cartridge_) {
            cartridge_->write_chr(addr, value);
        }
        return;
    }
    if (addr < 0x3F00) {
        vram_[mirror_nametable_address(addr)] = value;
        return;
    }
    write_palette(addr, value);
}

Address Ppu::mirror_nametable_address(Address addr) const {
    Address index = static_cast<Address>((addr - 0x2000) % 0x1000);
    Address table = index / 0x0400;
    Address offset = index % 0x0400;

    MirrorMode mirror_mode = cartridge_ ? cartridge_->mirror_mode() : MirrorMode::Horizontal;
    Address physical_table = 0;

    switch (mirror_mode) {
    case MirrorMode::Horizontal:
        physical_table = (table < 2) ? 0 : 1;
        break;
    case MirrorMode::Vertical:
        physical_table = table & 0x01;
        break;
    case MirrorMode::SingleLower:
        physical_table = 0;
        break;
    case MirrorMode::SingleUpper:
        physical_table = 1;
        break;
    case MirrorMode::FourScreen:
        physical_table = table;
        break;
    }

    return static_cast<Address>((physical_table * 0x0400 + offset) % vram_.size());
}

Byte Ppu::read_palette(Address addr) const {
    Address index = static_cast<Address>((addr - 0x3F00) % 32);
    if (index == 0x10 || index == 0x14 || index == 0x18 || index == 0x1C) {
        index = static_cast<Address>(index - 0x10);
    }
    return palette_[index];
}

void Ppu::write_palette(Address addr, Byte value) {
    Address index = static_cast<Address>((addr - 0x3F00) % 32);
    if (index == 0x10 || index == 0x14 || index == 0x18 || index == 0x1C) {
        index = static_cast<Address>(index - 0x10);
    }
    palette_[index] = static_cast<Byte>(value & 0x3F);
}

std::uint32_t Ppu::palette_color(Byte palette_value) const {
    return kNesPalette[palette_value & 0x3F];
}

// === Scanline-based rendering ===

void Ppu::render_scanline() {
    int y = scanline_;
    if (y < 0 || y >= kScreenHeight)
        return;

    bool show_bg = (ppumask_ & 0x08) != 0;
    bool show_sp = (ppumask_ & 0x10) != 0;

    std::array<bool, kScreenWidth> bg_opaque{};

    if (show_bg) {
        render_background_scanline(y, bg_opaque);
    } else {
        Byte universal_bg = read_palette(0x3F00);
        auto color = palette_color(universal_bg);
        for (int x = 0; x < kScreenWidth; ++x) {
            frame_buffer_.pixels[y * kScreenWidth + x] = color;
        }
    }

    if (show_sp) {
        render_sprites_scanline(y, bg_opaque);
    }
}

void Ppu::render_background_scanline(int y, std::array<bool, kScreenWidth>& bg_opaque) {
    Address base_nametable = static_cast<Address>(0x2000 | ((ppuctrl_ & 0x03) * 0x0400));
    Address pattern_base = (ppuctrl_ & 0x10) ? 0x1000 : 0x0000;
    Byte universal_bg = read_palette(0x3F00);
    bool clip_left = (ppumask_ & 0x02) == 0;

    int world_y = base_nametable >= 0x2800 ? y + scroll_y_ + 240 : y + scroll_y_;
    int wrapped_y = ((world_y % 480) + 480) % 480;
    int nametable_y = wrapped_y / 240;
    int local_y = wrapped_y % 240;
    int tile_y = local_y / 8;
    int fine_y = local_y % 8;

    for (int x = 0; x < kScreenWidth; ++x) {
        if (clip_left && x < 8) {
            frame_buffer_.pixels[y * kScreenWidth + x] = palette_color(universal_bg);
            bg_opaque[x] = false;
            continue;
        }

        int world_x =
            ((base_nametable == 0x2400 || base_nametable == 0x2C00) ? 256 : 0) + x + scroll_x_;
        int wrapped_x = ((world_x % 512) + 512) % 512;
        int nametable_x = wrapped_x / 256;
        int local_x = wrapped_x % 256;
        int tile_x = local_x / 8;
        int fine_x = local_x % 8;

        Address nametable_addr =
            static_cast<Address>(0x2000 + nametable_y * 0x0800 + nametable_x * 0x0400);
        Address tile_addr = static_cast<Address>(nametable_addr + tile_y * 32 + tile_x);
        Byte tile_index = read_vram(tile_addr);

        Address attribute_addr =
            static_cast<Address>(nametable_addr + 0x03C0 + (tile_y / 4) * 8 + (tile_x / 4));
        Byte attribute = read_vram(attribute_addr);
        int quadrant_shift = ((tile_y & 0x02) ? 4 : 0) | ((tile_x & 0x02) ? 2 : 0);
        Byte palette_index = static_cast<Byte>((attribute >> quadrant_shift) & 0x03);

        Address pattern_addr = static_cast<Address>(pattern_base + tile_index * 16 + fine_y);
        Byte plane0 = cartridge_ ? cartridge_->read_chr(pattern_addr) : 0;
        Byte plane1 = cartridge_ ? cartridge_->read_chr(pattern_addr + 8) : 0;
        Byte bit = static_cast<Byte>(7 - fine_x);
        Byte color_index =
            static_cast<Byte>(((plane0 >> bit) & 0x01) | (((plane1 >> bit) & 0x01) << 1));

        bg_opaque[x] = color_index != 0;

        Byte palette_value = universal_bg;
        if (color_index != 0) {
            Address pal_addr = static_cast<Address>(0x3F00 + palette_index * 4 + color_index);
            palette_value = read_palette(pal_addr);
        }
        frame_buffer_.pixels[y * kScreenWidth + x] = palette_color(palette_value);
    }
}

void Ppu::render_sprites_scanline(int y, const std::array<bool, kScreenWidth>& bg_opaque) {
    bool sprite_size_8x16 = (ppuctrl_ & 0x20) != 0;
    Address sprite_pattern_base = (ppuctrl_ & 0x08) ? 0x1000 : 0x0000;
    int sprite_height = sprite_size_8x16 ? 16 : 8;
    bool clip_left = (ppumask_ & 0x04) == 0;

    // Collect sprites visible on this scanline (up to 8, with overflow detection)
    struct SpriteEntry {
        int index;
        Byte y_pos;
        Byte tile;
        Byte attr;
        Byte x_pos;
    };

    std::array<SpriteEntry, 8> visible{};
    int visible_count = 0;

    for (int i = 0; i < 64; ++i) {
        Byte sy = oam_[i * 4];
        int top = static_cast<int>(sy) + 1;
        if (y >= top && y < top + sprite_height) {
            if (visible_count < 8) {
                visible[visible_count] = {
                    .index = i,
                    .y_pos = sy,
                    .tile = oam_[i * 4 + 1],
                    .attr = oam_[i * 4 + 2],
                    .x_pos = oam_[i * 4 + 3],
                };
                ++visible_count;
            } else {
                ppustatus_ |= 0x20; // Sprite overflow
                break;
            }
        }
    }

    // Render in reverse order (lowest index = highest priority, drawn last)
    for (int si = visible_count - 1; si >= 0; --si) {
        auto& sp = visible[si];
        int top = static_cast<int>(sp.y_pos) + 1;
        int row = y - top;

        bool flip_h = (sp.attr & 0x40) != 0;
        bool flip_v = (sp.attr & 0x80) != 0;
        bool behind_bg = (sp.attr & 0x20) != 0;
        Byte palette_idx = static_cast<Byte>(sp.attr & 0x03);

        int pattern_row = flip_v ? (sprite_height - 1 - row) : row;
        Address pat_base = sprite_pattern_base;
        Byte tile = sp.tile;

        if (sprite_size_8x16) {
            pat_base = static_cast<Address>((sp.tile & 0x01) * 0x1000);
            tile = static_cast<Byte>(sp.tile & 0xFE);
            if (pattern_row >= 8) {
                ++tile;
                pattern_row -= 8;
            }
        }

        Address pat_addr = static_cast<Address>(pat_base + tile * 16 + pattern_row);
        Byte plane0 = cartridge_ ? cartridge_->read_chr(pat_addr) : 0;
        Byte plane1 = cartridge_ ? cartridge_->read_chr(pat_addr + 8) : 0;

        for (int col = 0; col < 8; ++col) {
            int screen_x = static_cast<int>(sp.x_pos) + col;
            if (screen_x < 0 || screen_x >= kScreenWidth)
                continue;

            // Left-column sprite clipping
            if (clip_left && screen_x < 8)
                continue;

            int bit_index = flip_h ? col : (7 - col);
            Byte color_index = static_cast<Byte>(((plane0 >> bit_index) & 0x01) |
                                                 (((plane1 >> bit_index) & 0x01) << 1));
            if (color_index == 0)
                continue;

            // Sprite 0 hit: immediate detection on this scanline
            if (sp.index == 0 && bg_opaque[screen_x] && screen_x != 255) {
                ppustatus_ |= 0x40;
            }

            std::size_t fb_idx = static_cast<std::size_t>(y) * kScreenWidth + screen_x;
            if (behind_bg && bg_opaque[screen_x])
                continue;

            Address pal_addr = static_cast<Address>(0x3F10 + palette_idx * 4 + color_index);
            frame_buffer_.pixels[fb_idx] = palette_color(read_palette(pal_addr));
        }
    }
}

} // namespace mapperbus::core
