#include "core/mappers/mapper156_daou.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr uint16_t kStateTag = 0x9C00U;

} // namespace

// Original implementation from the bank and register tables in NESdev mapper 156 r22838.
DaouMapper156::DaouMapper156(const INesHeader&,
                             std::vector<Byte> prg_rom,
                             std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    power_on();
}

Result<void> DaouMapper156::validate_geometry(const INesHeader& header,
                                              std::size_t prg_size,
                                              std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 156 supports submapper 0");
    if (prg_size < 0x8000U || prg_size > 0x40000U || prg_size % 0x4000U != 0U)
        return std::unexpected("Mapper 156 requires 32-256 KiB PRG ROM in 16 KiB units");
    if (chr_size < 0x2000U || chr_size > 0x80000U || chr_size % 0x400U != 0U)
        return std::unexpected("Mapper 156 requires 8-512 KiB CHR ROM in 1 KiB units");
    if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0x2000U || header.prg_nvram_size != 0U)))
        return std::unexpected("Mapper 156 requires 8 KiB volatile PRG RAM and no other RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 156 requires H/V header mirroring");
    return {};
}

Byte DaouMapper156::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U)
        return prg_ram_[addr & 0x1FFFU];
    if (addr < 0x8000U)
        return 0U;
    if (addr >= 0xC000U)
        return prg_rom_[prg_rom_.size() - 0x4000U + (addr & 0x3FFFU)];
    const std::size_t offset = static_cast<std::size_t>(prg_bank_) * 0x4000U + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void DaouMapper156::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        prg_ram_[addr & 0x1FFFU] = value;
        return;
    }
    if (addr >= 0xC000U && addr <= 0xC00FU) {
        const auto slot = static_cast<std::size_t>(addr & 3U) | ((addr & 8U) != 0U ? 4U : 0U);
        if ((addr & 4U) == 0U)
            chr_banks_[slot] = static_cast<uint16_t>((chr_banks_[slot] & 0x100U) | value);
        else
            chr_banks_[slot] =
                static_cast<uint16_t>((chr_banks_[slot] & 0xFFU) | ((value & 1U) << 8U));
    } else if (addr == 0xC010U) {
        prg_bank_ = value & 0x0FU;
    } else if (addr == 0xC014U) {
        mirror_ = value & 3U;
    }
}

bool DaouMapper156::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte DaouMapper156::read_chr(Address addr) {
    const auto slot = static_cast<std::size_t>((addr >> 10U) & 7U);
    const std::size_t offset =
        static_cast<std::size_t>(chr_banks_[slot]) * 0x400U + (addr & 0x3FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void DaouMapper156::write_chr(Address, Byte) {}

MirrorMode DaouMapper156::mirror_mode() const {
    switch (mirror_) {
    case 0U:
        return MirrorMode::Vertical;
    case 1U:
        return MirrorMode::Horizontal;
    default:
        return MirrorMode::SingleLower;
    }
}

void DaouMapper156::power_on() {
    reset();
    prg_ram_.fill(0U);
}

void DaouMapper156::reset() {
    chr_banks_.fill(0U);
    prg_bank_ = 0U;
    mirror_ = 2U;
}

void DaouMapper156::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write_array(chr_banks_);
    writer.write(prg_bank_);
    writer.write(mirror_);
    writer.write_array(prg_ram_);
}

void DaouMapper156::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    std::array<uint16_t, 8> chr_banks{};
    reader.read_array(chr_banks);
    const auto prg_bank = reader.read<Byte>();
    const auto mirror = reader.read<Byte>();
    std::array<Byte, 0x2000> prg_ram{};
    reader.read_array(prg_ram);
    if (!reader.ok() || tag != kStateTag || prg_bank > 0x0FU || mirror > 3U ||
        std::ranges::any_of(chr_banks, [](uint16_t bank) { return bank > 0x1FFU; })) {
        reader.fail();
        return;
    }
    chr_banks_ = chr_banks;
    prg_bank_ = prg_bank;
    mirror_ = mirror;
    prg_ram_ = prg_ram;
}

} // namespace mapperbus::core
