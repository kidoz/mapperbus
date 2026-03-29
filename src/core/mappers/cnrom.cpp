#include "core/mappers/cnrom.hpp"

namespace mapperbus::core {

Cnrom::Cnrom(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), mirror_mode_(header.mirror_mode) {
    num_chr_banks_ = static_cast<uint8_t>(chr_rom_.empty() ? 1 : chr_rom_.size() / 0x2000);
}

Byte Cnrom::read_prg(Address addr) {
    if (addr < 0x8000)
        return 0;
    std::size_t offset = (addr - 0x8000) % prg_rom_.size();
    return prg_rom_[offset];
}

void Cnrom::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000) {
        bank_select_ = value & (num_chr_banks_ - 1);
    }
}

Byte Cnrom::read_chr(Address addr) {
    if (chr_rom_.empty())
        return 0;
    std::size_t offset = (bank_select_ * 0x2000) + (addr % 0x2000);
    return chr_rom_[offset % chr_rom_.size()];
}

void Cnrom::write_chr([[maybe_unused]] Address addr, [[maybe_unused]] Byte value) {
    // CHR ROM is read-only
}

MirrorMode Cnrom::mirror_mode() const {
    return mirror_mode_;
}

void Cnrom::reset() {
    bank_select_ = 0;
}

} // namespace mapperbus::core
