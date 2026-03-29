#include "core/mappers/uxrom.hpp"

namespace mapperbus::core {

Uxrom::Uxrom(const INesHeader& header,
             std::vector<Byte> prg_rom,
             [[maybe_unused]] std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), mirror_mode_(header.mirror_mode) {
    num_banks_ = static_cast<uint8_t>(prg_rom_.size() / 0x4000);
}

Byte Uxrom::read_prg(Address addr) {
    if (addr < 0x8000)
        return 0;
    if (addr < 0xC000) {
        // Switchable 16 KB bank
        std::size_t offset = (bank_select_ * 0x4000) + (addr - 0x8000);
        return prg_rom_[offset % prg_rom_.size()];
    }
    // Fixed last 16 KB bank
    std::size_t offset = ((num_banks_ - 1) * 0x4000) + (addr - 0xC000);
    return prg_rom_[offset % prg_rom_.size()];
}

void Uxrom::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000) {
        bank_select_ = value & (num_banks_ - 1);
    }
}

Byte Uxrom::read_chr(Address addr) {
    return chr_ram_[addr % 0x2000];
}

void Uxrom::write_chr(Address addr, Byte value) {
    chr_ram_[addr % 0x2000] = value;
}

MirrorMode Uxrom::mirror_mode() const {
    return mirror_mode_;
}

void Uxrom::reset() {
    bank_select_ = 0;
}

} // namespace mapperbus::core
