#include "core/mappers/nrom.hpp"

namespace mapperbus::core {

Nrom::Nrom(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), mirror_mode_(header.mirror_mode),
      use_chr_ram_(chr_rom_.empty()) {
    prg_ram_.fill(0);
    chr_ram_.fill(0);
}

Byte Nrom::read_prg(Address addr) {
    if (addr >= 0x6000 && addr < 0x8000) {
        return prg_ram_[addr - 0x6000];
    }
    if (addr < 0x8000)
        return 0;
    // NROM-128: 16 KB mirrored, NROM-256: 32 KB
    std::size_t offset = (addr - 0x8000) % prg_rom_.size();
    return prg_rom_[offset];
}

void Nrom::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000 && addr < 0x8000) {
        prg_ram_[addr - 0x6000] = value;
    }
}

bool Nrom::maps_prg(Address addr) const {
    return addr >= 0x6000;
}

Byte Nrom::read_chr(Address addr) {
    std::size_t offset = addr % 0x2000;
    if (use_chr_ram_) {
        return chr_ram_[offset];
    }
    if (offset < chr_rom_.size()) {
        return chr_rom_[offset];
    }
    return 0;
}

void Nrom::write_chr(Address addr, Byte value) {
    if (use_chr_ram_) {
        chr_ram_[addr % 0x2000] = value;
    }
    // CHR ROM is read-only
}

MirrorMode Nrom::mirror_mode() const {
    return mirror_mode_;
}

void Nrom::reset() {
    prg_ram_.fill(0);
}

} // namespace mapperbus::core
