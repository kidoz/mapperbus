#include "core/mappers/mapper190.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

// Original implementation from the register and bank specification in NESdev
// mapper 190 r22728. Smaller fitted ROMs ignore unconnected high address pins.
Mapper190::Mapper190(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper190::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 190 supports submapper 0");
    if (prg_size < 0x8000U || prg_size > 0x40000U || prg_size % 0x4000U != 0U)
        return std::unexpected("Mapper 190 requires 32-256 KiB PRG ROM in 16 KiB units");
    if (chr_size < 0x2000U || chr_size > 0x80000U || chr_size % 0x800U != 0U)
        return std::unexpected("Mapper 190 requires 8-512 KiB CHR ROM in 2 KiB units");
    if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0x2000U || header.prg_nvram_size != 0U)))
        return std::unexpected("Mapper 190 requires 8 KiB volatile PRG RAM and no battery memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 190 requires fixed H/V mirroring");
    return {};
}

Byte Mapper190::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U)
        return prg_ram_[addr & 0x1FFFU];
    if (addr < 0x8000U)
        return 0U;
    const std::size_t offset =
        addr < 0xC000U ? static_cast<std::size_t>(prg_bank_) * 0x4000U + (addr & 0x3FFFU)
                       : addr & 0x3FFFU;
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper190::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        prg_ram_[addr & 0x1FFFU] = value;
        return;
    }
    if ((addr & 0xE000U) == 0xA000U) {
        chr_banks_[addr & 3U] = value;
    } else if ((addr & 0xA000U) == 0x8000U && addr >= 0x8000U) {
        prg_bank_ = static_cast<Byte>((value & 7U) | ((addr >> 11U) & 8U));
    }
}

bool Mapper190::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper190::read_chr(Address addr) {
    const auto bank = chr_banks_[(addr >> 11U) & 3U];
    const std::size_t offset = static_cast<std::size_t>(bank) * 0x800U + (addr & 0x7FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper190::write_chr(Address, Byte) {}

MirrorMode Mapper190::mirror_mode() const {
    return mirror_;
}

void Mapper190::power_on() {
    reset();
    prg_ram_.fill(0U);
}

void Mapper190::reset() {
    prg_bank_ = 0U;
    chr_banks_.fill(0U);
}

uint16_t Mapper190::state_tag() const {
    return static_cast<uint16_t>(0xBE00U | (mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper190::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(prg_bank_);
    writer.write_array(chr_banks_);
    writer.write_array(prg_ram_);
}

void Mapper190::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_bank = reader.read<Byte>();
    std::array<Byte, 4> chr_banks{};
    std::array<Byte, 0x2000> prg_ram{};
    reader.read_array(chr_banks);
    reader.read_array(prg_ram);
    if (!reader.ok() || tag != state_tag() || prg_bank > 15U) {
        reader.fail();
        return;
    }
    prg_bank_ = prg_bank;
    chr_banks_ = chr_banks;
    prg_ram_ = prg_ram;
}

} // namespace mapperbus::core
