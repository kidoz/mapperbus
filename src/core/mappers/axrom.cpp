#include "core/mappers/axrom.hpp"

namespace mapperbus::core {

Axrom::Axrom(const INesHeader& header,
             std::vector<Byte> prg_rom,
             [[maybe_unused]] std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), mirror_mode_(MirrorMode::SingleLower),
      has_bus_conflicts_(header.is_nes2 && header.submapper == 2) {
    num_banks_ = static_cast<uint8_t>(prg_rom_.size() / 0x8000);
}

Byte Axrom::read_prg(Address addr) {
    if (addr < 0x8000)
        return 0;
    std::size_t offset = (bank_select_ * 0x8000) + (addr - 0x8000);
    return prg_rom_[offset % prg_rom_.size()];
}

void Axrom::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000) {
        if (has_bus_conflicts_) {
            value &= read_prg(addr);
        }
        bank_select_ = value & (num_banks_ - 1);
        // Bit 4 selects single-screen nametable
        mirror_mode_ = (value & 0x10) ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    }
}

Byte Axrom::read_chr(Address addr) {
    return chr_ram_[addr % 0x2000];
}

void Axrom::write_chr(Address addr, Byte value) {
    chr_ram_[addr % 0x2000] = value;
}

MirrorMode Axrom::mirror_mode() const {
    return mirror_mode_;
}

void Axrom::reset() {
    bank_select_ = 0;
    mirror_mode_ = MirrorMode::SingleLower;
}

} // namespace mapperbus::core
