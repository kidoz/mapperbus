#include "core/mappers/irem_lrog017.hpp"

#include "core/state/state.hpp"

namespace mapperbus::core {

// NESdev mapper 77 r24538; LROG017 board identification in BootGod's dump notes.
IremLrog017::IremLrog017(const INesHeader&, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_rom_(std::move(prg)), chr_rom_(std::move(chr)) {}

Byte IremLrog017::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    const auto bank = static_cast<std::size_t>(prg_bank_);
    return prg_rom_[(bank * 0x8000U + (addr & 0x7FFFU)) % prg_rom_.size()];
}

void IremLrog017::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    // ROM still drives the pre-write bank while both latches sample CPU data.
    const auto bus = static_cast<unsigned>(value & read_prg(addr));
    prg_bank_ = static_cast<Byte>(bus & 15U);
    chr_bank_ = static_cast<Byte>(bus >> 4U);
}

Byte IremLrog017::read_chr(Address addr) {
    const auto offset = static_cast<unsigned>(addr) & 0x1FFFU;
    if (offset >= 0x800U)
        return vram_[offset];
    const auto bank = static_cast<std::size_t>(chr_bank_);
    return chr_rom_[(bank * 0x800U + offset) % chr_rom_.size()];
}

void IremLrog017::write_chr(Address addr, Byte value) {
    const auto offset = static_cast<unsigned>(addr) & 0x1FFFU;
    if (offset >= 0x800U)
        vram_[offset] = value;
}

MirrorMode IremLrog017::mirror_mode() const {
    return MirrorMode::FourScreen;
}

bool IremLrog017::maps_nametable(Address addr) const {
    // The 74138 fully decodes PPU A10-A13; $3000 is not a $2000 mirror.
    return addr >= 0x2000U && addr < 0x3000U;
}

std::optional<Address> IremLrog017::nametable_address(Address addr) const {
    // The last two nametables use the actual console's two CIRAM pages.
    return static_cast<Address>(addr & 0x7FFU);
}

std::optional<Byte> IremLrog017::read_nametable(Address addr) const {
    if (addr >= 0x2000U && addr < 0x2800U)
        return vram_[addr & 0x7FFU];
    return std::nullopt;
}

bool IremLrog017::write_nametable(Address addr, Byte value) {
    if (addr < 0x2000U || addr >= 0x2800U)
        return false;
    vram_[addr & 0x7FFU] = value;
    return true;
}

void IremLrog017::reset() {
    // Deterministic bank defaults. Cartridge VRAM is retained across reset;
    // console CIRAM remains owned and reset by the PPU.
    prg_bank_ = 0U;
    chr_bank_ = 0U;
}

void IremLrog017::save_state(StateWriter& writer) const {
    writer.write(prg_bank_);
    writer.write(chr_bank_);
    writer.write_array(vram_);
}

void IremLrog017::load_state(StateReader& reader) {
    reader.read(prg_bank_);
    reader.read(chr_bank_);
    reader.read_array(vram_);
    if (!reader.ok() || prg_bank_ > 15U || chr_bank_ > 15U) {
        reset();
        reader.fail();
    }
}

} // namespace mapperbus::core
