#include "core/mappers/color_dreams.hpp"

namespace mapperbus::core {

ColorDreams::ColorDreams(const INesHeader& header,
                         std::vector<Byte> prg_rom,
                         std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), mirror_mode_(header.mirror_mode),
      use_chr_ram_(chr_rom_.empty()) {}

Byte ColorDreams::read_prg(Address addr) {
    if (addr < 0x8000)
        return 0;
    std::size_t offset = (prg_bank_ * 0x8000) + (addr - 0x8000);
    return prg_rom_[offset % prg_rom_.size()];
}

void ColorDreams::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000) {
        prg_bank_ = value & 0x03;
        chr_bank_ = (value >> 4) & 0x0F;
    }
}

Byte ColorDreams::read_chr(Address addr) {
    if (use_chr_ram_) {
        return chr_ram_[addr % 0x2000];
    }
    std::size_t offset = (chr_bank_ * 0x2000) + (addr % 0x2000);
    return chr_rom_[offset % chr_rom_.size()];
}

void ColorDreams::write_chr(Address addr, Byte value) {
    if (use_chr_ram_) {
        chr_ram_[addr % 0x2000] = value;
    }
}

MirrorMode ColorDreams::mirror_mode() const {
    return mirror_mode_;
}

void ColorDreams::reset() {
    prg_bank_ = 0;
    chr_bank_ = 0;
}

} // namespace mapperbus::core
