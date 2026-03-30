#include "core/ppu/ppu.hpp"

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
    read_buffer_ = 0;
    nmi_pending_ = false;
    reg_v_ = 0;
    reg_t_ = 0;
    fine_x_ = 0;
    write_latch_ = false;
    oam_.fill(0);
    vram_.fill(0);
    palette_.fill(0);
    frame_buffer_.pixels.fill(0xFF000000);
}

// === Loopy register operations (per Mesen2/FCEUX/NESDev wiki) ===

void Ppu::increment_coarse_x() {
    if ((reg_v_ & 0x001F) == 31) {
        reg_v_ &= ~0x001F;
        reg_v_ ^= 0x0400; // toggle horizontal nametable
    } else {
        ++reg_v_;
    }
}

void Ppu::increment_fine_y() {
    if ((reg_v_ & 0x7000) != 0x7000) {
        reg_v_ += 0x1000;
    } else {
        reg_v_ &= ~0x7000;
        int y = (reg_v_ >> 5) & 0x1F;
        if (y == 29) {
            y = 0;
            reg_v_ ^= 0x0800; // toggle vertical nametable
        } else if (y == 31) {
            y = 0; // wrap without nametable toggle
        } else {
            ++y;
        }
        reg_v_ = (reg_v_ & ~0x03E0) | (y << 5);
    }
}

void Ppu::copy_horizontal_from_t() {
    // v: ....A.. ...EDCBA = t: ....A.. ...EDCBA
    reg_v_ = (reg_v_ & ~0x041F) | (reg_t_ & 0x041F);
}

void Ppu::copy_vertical_from_t() {
    // v: GHIA.BC DEF..... = t: GHIA.BC DEF.....
    reg_v_ = (reg_v_ & ~0x7BE0) | (reg_t_ & 0x7BE0);
}

// === PPU step ===

void Ppu::step(uint32_t cpu_cycles) {
    uint32_t ppu_dots = cpu_cycles * 3;
    for (uint32_t i = 0; i < ppu_dots; ++i) {
        ++cycle_;

        bool visible_line = scanline_ < 240;
        bool pre_render = scanline_ == timing_->pre_render_scanline;
        bool rendering = rendering_enabled();

        // Visible scanlines: render THEN update v for next scanline.
        // Rendering uses current v. After rendering, v is advanced.
        if (visible_line && cycle_ == 257) {
            render_scanline();
            if (rendering) {
                increment_fine_y();
                copy_horizontal_from_t();
            }
        }

        // Pre-render scanline: restore v from t for the new frame.
        // Gated on rendering_enabled — games that disable rendering during
        // VBlank for VRAM access may have $2006 values in t that would corrupt
        // the scroll if copied unconditionally. Games re-enable rendering
        // before this point, so t has the correct scroll from $2005 writes.
        if (rendering && pre_render) {
            if (cycle_ == 257) {
                copy_horizontal_from_t();
            }
            if (cycle_ >= 280 && cycle_ <= 304) {
                copy_vertical_from_t();
            }
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
        if (pre_render && cycle_ == 1) {
            ppustatus_ &= ~0xE0;
            frame_ready_ = false;
        }

        // Clock mapper IRQ counter (A12 rising edge, cycle 260)
        if (cycle_ == 260 && rendering && cartridge_) {
            if (visible_line || pre_render) {
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

// === Register I/O (pure loopy model) ===

Byte Ppu::read_register(uint8_t reg) {
    reg &= 0x07;
    switch (reg) {
    case 0x02: { // PPUSTATUS
        Byte value = static_cast<Byte>((ppustatus_ & 0xE0) | (read_buffer_ & 0x1F));
        ppustatus_ &= ~0x80;
        write_latch_ = false; // Reset w
        nmi_pending_ = false;
        return value;
    }
    case 0x04: // OAMDATA
        return oam_[oam_addr_];
    case 0x07: { // PPUDATA
        Byte value = read_vram(reg_v_);
        Byte result = 0;
        if ((reg_v_ & 0x3FFF) >= 0x3F00) {
            result = value;
            read_buffer_ = read_vram(static_cast<Address>(reg_v_ - 0x1000));
        } else {
            result = read_buffer_;
            read_buffer_ = value;
        }
        reg_v_ = (reg_v_ + ((ppuctrl_ & 0x04) ? 32 : 1)) & 0x7FFF;
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
    case 0x00: // PPUCTRL
        ppuctrl_ = value;
        // t: ...GH.. ........ = d: ......GH
        reg_t_ = (reg_t_ & ~0x0C00) | ((static_cast<uint16_t>(value) & 0x03) << 10);
        if ((ppuctrl_ & 0x80) != 0 && (ppustatus_ & 0x80) != 0) {
            nmi_pending_ = true;
        }
        break;
    case 0x01: // PPUMASK
        ppumask_ = value;
        break;
    case 0x03: // OAMADDR
        oam_addr_ = value;
        break;
    case 0x04: // OAMDATA
        oam_[oam_addr_++] = value;
        break;
    case 0x05: // PPUSCROLL
        if (!write_latch_) {
            // First write: t coarse X + fine X
            reg_t_ = (reg_t_ & ~0x001F) | (value >> 3);
            fine_x_ = value & 0x07;
        } else {
            // Second write: t fine Y + coarse Y
            reg_t_ = (reg_t_ & ~0x73E0) | ((static_cast<uint16_t>(value) & 0x07) << 12) |
                     ((static_cast<uint16_t>(value) & 0xF8) << 2);
        }
        write_latch_ = !write_latch_;
        break;
    case 0x06: // PPUADDR
        if (!write_latch_) {
            // First write: t high byte, clear bit 14
            reg_t_ = (reg_t_ & 0x00FF) | ((static_cast<uint16_t>(value) & 0x3F) << 8);
        } else {
            // Second write: t low byte, then v = t
            reg_t_ = (reg_t_ & 0xFF00) | value;
            reg_v_ = reg_t_;
        }
        write_latch_ = !write_latch_;
        break;
    case 0x07: // PPUDATA
        write_vram(reg_v_, value);
        reg_v_ = (reg_v_ + ((ppuctrl_ & 0x04) ? 32 : 1)) & 0x7FFF;
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

// === VRAM access ===

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
    // Tile fetches use v directly (per Mesen2/FCEUX).
    //   tile_addr = 0x2000 | (v & 0x0FFF)
    //   attr_addr = 0x23C0 | (v & 0x0C00) | ((v >> 4) & 0x38) | ((v >> 2) & 0x07)
    //   fine_y    = (v >> 12) & 0x07
    // Coarse X is incremented after every 8 pixels.
    // fine_x_ provides the sub-pixel horizontal offset within the first tile.

    Address pattern_base = (ppuctrl_ & 0x10) ? 0x1000 : 0x0000;
    Byte universal_bg = read_palette(0x3F00);
    bool clip_left_bg = (ppumask_ & 0x02) == 0;

    // Work on a local copy of v for the scanline; the real reg_v_ is updated
    // by increment_fine_y / copy_horizontal in the step() function AFTER render.
    uint16_t v = reg_v_;

    for (int x = 0; x < kScreenWidth; ++x) {
        // Left-column BG clipping
        if (clip_left_bg && x < 8) {
            frame_buffer_.pixels[y * kScreenWidth + x] = palette_color(universal_bg);
            bg_opaque[x] = false;
            // Still advance coarse X at tile boundaries
            if (((x + fine_x_) & 0x07) == 7) {
                if ((v & 0x001F) == 31) {
                    v &= ~0x001F;
                    v ^= 0x0400;
                } else {
                    ++v;
                }
            }
            continue;
        }

        // Derive tile address from v
        Address tile_addr = static_cast<Address>(0x2000 | (v & 0x0FFF));
        Byte tile_index = read_vram(tile_addr);

        // Derive attribute address from v
        Address attr_addr = static_cast<Address>(0x23C0 | (v & 0x0C00) |
                                                 ((v >> 4) & 0x38) | ((v >> 2) & 0x07));
        Byte attribute = read_vram(attr_addr);

        int coarse_x = v & 0x1F;
        int coarse_y = (v >> 5) & 0x1F;
        int shift = ((coarse_y & 0x02) ? 4 : 0) | ((coarse_x & 0x02) ? 2 : 0);
        Byte palette_index = static_cast<Byte>((attribute >> shift) & 0x03);

        // Pattern table lookup using fine Y from v
        int fine_y = (v >> 12) & 0x07;
        Address pattern_addr = static_cast<Address>(pattern_base + tile_index * 16 + fine_y);
        Byte plane0 = cartridge_ ? cartridge_->read_chr(pattern_addr) : 0;
        Byte plane1 = cartridge_ ? cartridge_->read_chr(pattern_addr + 8) : 0;

        // Pixel within tile
        int pixel_in_tile = (x + fine_x_) & 0x07;
        Byte bit = static_cast<Byte>(7 - pixel_in_tile);
        Byte color_index =
            static_cast<Byte>(((plane0 >> bit) & 0x01) | (((plane1 >> bit) & 0x01) << 1));

        bg_opaque[x] = color_index != 0;

        Byte palette_value = universal_bg;
        if (color_index != 0) {
            Address pal_addr = static_cast<Address>(0x3F00 + palette_index * 4 + color_index);
            palette_value = read_palette(pal_addr);
        }
        frame_buffer_.pixels[y * kScreenWidth + x] = palette_color(palette_value);

        // Increment coarse X at tile boundaries
        if (pixel_in_tile == 7) {
            if ((v & 0x001F) == 31) {
                v &= ~0x001F;
                v ^= 0x0400;
            } else {
                ++v;
            }
        }
    }
}

void Ppu::render_sprites_scanline(int y, const std::array<bool, kScreenWidth>& bg_opaque) {
    bool sprite_size_8x16 = (ppuctrl_ & 0x20) != 0;
    Address sprite_pattern_base = (ppuctrl_ & 0x08) ? 0x1000 : 0x0000;
    int sprite_height = sprite_size_8x16 ? 16 : 8;
    bool clip_left_sp = (ppumask_ & 0x04) == 0;

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
                ppustatus_ |= 0x20;
                break;
            }
        }
    }

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

            if (clip_left_sp && screen_x < 8)
                continue;

            int bit_index = flip_h ? col : (7 - col);
            Byte color_index = static_cast<Byte>(((plane0 >> bit_index) & 0x01) |
                                                 (((plane1 >> bit_index) & 0x01) << 1));
            if (color_index == 0)
                continue;

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
