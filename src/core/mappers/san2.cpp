#include "core/mappers/san2.hpp"

#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
// In the 29,920-byte FONT chunk, groups 6–14 use two consecutive runs.
// Group 1 is the translation's separate dictionary of interface symbols.
unsigned glyph_index(Byte group, Byte tile) {
    if (group >= 6)
        return tile < 64 ? (group - 6) * 49 + tile - 15 : 441 + (group - 6) * 54 + tile - 64;
    constexpr std::array<unsigned, 46> symbols{
        304, 305, 294, 0,  0,   337, 0, 0,   927, 306, 200, 307, 308, 309, 928, 929,
        930, 338, 0,   0,  0,   0,   0, 0,   255, 845, 312, 197, 603, 450, 931, 932,
        23,  933, 868, 41, 288, 0,   0, 934, 785, 927, 927, 927, 927, 927};
    if (group != 1)
        return 0;
    if (tile >= 102 && tile <= 107)
        return 927;
    return tile >= 56 && tile < 102 ? symbols[tile - 56] : 0;
}
struct TextWindow {
    int first = 30, row_offset = 0, scroll = 0, base = 0, limit = 118;
    bool special = false;
};
TextWindow window(const std::array<Byte, kRamSize>& ram) {
    TextWindow w;
    const int page = ram[0x9B] * 256 + ram[0x9A] - 0x2000;
    const int origin = (page + ram[0x97] * 0x400) / 32;
    if (ram[0x60] == 8 && ram[0xBD] == 0xB3) {
        w.first = 14;
        w.special = true;
        return w;
    }
    if (ram[0xBA] != 8)
        return w;
    w.limit = ram[0xBB] == 9 ? 118 : 64;
    switch (ram[0x60]) {
    case 1:
        w.first = 21;
        if (ram[0x6A] == 0x70) {
            w.scroll = (ram[0x98] & 7) + 2;
            w.row_offset = origin - w.first;
        } else {
            w.scroll = ram[0x98] <= ram[0x99] ? (ram[0x98] + 2) & 7 : ((ram[0x98] + 6) & 7) + 4;
            w.row_offset = page / 32 - w.first;
        }
        break;
    case 3:
        w.first = 20;
        w.row_offset = page / 32 - w.first;
        break;
    case 5:
        w.first = ram[0xAF] == 8 ? 0 : 20;
        w.row_offset = w.first == 0 ? 0 : 12;
        break;
    case 6:
        w.first = 0;
        w.base = 0x400;
        break;
    case 7:
        w.first = 1;
        for (int i = 0; i < 4; ++i)
            w.first +=
                ((0x7FFF - ((ram[0x69 + i * 2] & 127) * 256 + ram[0x68 + i * 2])) * 3) / 2728;
        w.row_offset = origin - w.first;
        break;
    case 10:
    case 11:
        w.first = 20;
        w.row_offset = origin - w.first;
        break;
    }
    return w;
}
} // namespace

Result<std::unique_ptr<Mapper>> San2::create(const INesHeader& h,
                                             std::vector<Byte> prg,
                                             std::vector<Byte> chr,
                                             std::span<const Byte> font) {
    if (!h.is_nes2 || h.submapper || h.console_type || h.has_trainer || !h.has_battery ||
        h.prg_ram_size || h.prg_nvram_size != 8192 || h.chr_ram_size || h.chr_nvram_size ||
        h.mirror_mode != MirrorMode::Horizontal || prg.size() != 262144 || chr.size() != 262144 ||
        h.misc_rom_count != 1 || font.size() != 29920)
        return std::unexpected(
            "CHINA_ER_SAN2 requires 256/256 KiB ROM, 8 KiB battery RAM and one 29920-byte font");
    return std::make_unique<San2>(h, std::move(prg), std::move(chr), font);
}
San2::San2(const INesHeader& h,
           std::vector<Byte> prg,
           std::vector<Byte> chr,
           std::span<const Byte> font)
    : Namco163(h, std::move(prg), std::move(chr)), font_(font.begin(), font.end()) {
    reset();
}
void San2::reset() {
    Namco163::reset();
    for (unsigned i = 0; i < 8; ++i)
        write_prg(0x8000 + i * 0x800, i);
    for (unsigned i = 0; i < 4; ++i)
        banks_[8 + i] = 0xE0 | (i & 1);
    for (unsigned i = 0; i < 3; ++i)
        Namco163::write_prg(0xE000 + i * 0x800, 0xFC + i);
    protect_ = 0xFF;
    force_rom_ = 0xC0;
    mute_ = false;
}
void San2::write_prg(Address a, Byte value) {
    if (a >= 0x6000 && a < 0x8000 && ((protect_ & (0xF0 | (1U << ((a - 0x6000) >> 11)))) != 0x40))
        return;
    if (a >= 0x8000 && a < 0xE000)
        banks_[(a - 0x8000) >> 11] = value;
    if ((a & 0xF800) == 0xE000)
        mute_ = (value & 0x40) != 0;
    if ((a & 0xF800) == 0xE800)
        force_rom_ = value & 0xC0;
    if (a >= 0xF800)
        protect_ = value;
    Namco163::write_prg(a, value);
}
std::optional<Address> San2::ciram_address(Address a) const {
    if (a >= 0x3F00)
        return {};
    unsigned slot = a < 0x2000 ? a >> 10 : 8 + ((a >> 10) & 3);
    if ((a < 0x2000 && (force_rom_ & (a < 0x1000 ? 0x40 : 0x80))) || banks_[slot] < 0xE0)
        return {};
    return (banks_[slot] & 1) * 1024 + (a & 1023);
}
std::optional<Byte> San2::read_nametable(Address a) const {
    return chr_rom_byte(banks_[8 + ((a >> 10) & 3)], a);
}
void San2::save_state(StateWriter& w) const {
    w.write_array(banks_);
    w.write(protect_);
    w.write(force_rom_);
    w.write(static_cast<Byte>(mute_));
    Namco163::save_state(w);
}
void San2::load_state(StateReader& r) {
    decltype(banks_) banks;
    r.read_array(banks);
    const Byte protect = r.read<Byte>(), force = r.read<Byte>(), mute = r.read<Byte>();
    if (!r.ok() || (force & 0x3F) || mute > 1) {
        r.fail();
        return;
    }
    StateWriter backup;
    Namco163::save_state(backup);
    Namco163::load_state(r);
    if (!r.ok()) {
        StateReader old(backup.data());
        Namco163::load_state(old);
        return;
    }
    banks_ = banks;
    protect_ = protect;
    force_rom_ = force;
    mute_ = mute != 0;
}

const FrameBuffer& San2::translated_frame(const Ppu& ppu,
                                          const std::array<Byte, kRamSize>& ram) const {
    auto w = window(ram);
    if (w.first >= 30)
        return ppu.frame_buffer();
    translated_ = ppu.frame_buffer();
    const auto& nt = ppu.vram();
    for (int screen = w.first; screen < 30; ++screen) {
        int row = screen + w.row_offset;
        if (row == 20 && ram[0x60] == 5 && ram[0xCB] == 9) {
            w.row_offset += 12;
            row += 12;
        }
        if (row < 0 || row >= 128)
            continue;
        int count = 0, offset = 0;
        Byte group = 1;
        for (int col = 0; col < 32; ++col) {
            int pos = row * 32 + col;
            const bool wrapped = pos > 0x800;
            pos += w.base - (wrapped ? 0x400 : 0);
            if (pos >= static_cast<int>(nt.size()))
                continue;
            Byte tile = nt[pos];
            if (tile < 15) {
                group = tile;
                if (group < 5)
                    count = offset = 0;
                else
                    offset += 8;
                continue;
            }
            if (w.special ? (tile <= 0xC7 || tile - 0x90 >= 118) : tile >= w.limit) {
                count = offset = 0;
                continue;
            }
            unsigned color = 7;
            if (w.special)
                tile -= 0x90;
            else {
                const auto attr = nt[0x3C0 + (row / 4) * 8 + col / 4];
                const unsigned shift = (row & 2) * 2 + (col & 2);
                color = ((attr >> shift) & 3) * 4;
            }
            const auto rgb = ppu.palette_color(ppu.palette_ram()[color]);
            const auto glyph = glyph_index(group, tile) * 32;
            const int x = col * 8 + count * 6 - offset,
                      y = screen * 8 - w.scroll - (wrapped ? 16 : 0) - 2;
            for (int dy = 0; dy < 16; ++dy)
                for (int dx = 0; dx < 16; ++dx) {
                    const int px = x + dx, py = y + dy;
                    if (px >= 0 && px < kScreenWidth && py >= 0 && py < kScreenHeight &&
                        (font_[glyph + dy * 2 + dx / 8] & (0x80 >> (dx & 7))))
                        translated_.pixels[py * kScreenWidth + px] = rgb;
                }
            ++count;
        }
    }
    return translated_;
}
} // namespace mapperbus::core
