#include "core/mappers/mapper309.hpp"

#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kBankSize = 8192U;
constexpr std::size_t kPrgRomSize = 16U * kBankSize;
constexpr std::uint16_t kStateTag = 0x3090U;

} // namespace

// Original implementation from NESdev mapper 309 revision 7795. Reset and
// power-on values are deterministic policies; the article does not specify them.
Mapper309::Mapper309(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper309::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 309U)
        return std::unexpected("Mapper309 requires mapper ID 309");
    if (!header.is_nes2 || header.submapper != 0U)
        return std::unexpected("Mapper 309 requires NES 2.0 submapper 0");
    if (prg_size != kPrgRomSize || chr_size != 0U)
        return std::unexpected("Mapper 309 requires 128 KiB PRG ROM and no CHR ROM");
    if (header.has_battery || header.prg_ram_size != kBankSize || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kBankSize || header.chr_nvram_size != 0U)
        return std::unexpected("Mapper 309 requires 8 KiB volatile PRG RAM and 8 KiB CHR RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 309 supports ordinary H/V CIRAM metadata");
    return {};
}

Byte Mapper309::read_prg(Address addr) {
    if (addr < 0x6000U)
        return 0U;
    if (addr < 0x8000U)
        return prg_ram_[addr & 0x1FFFU];

    Byte bank = prg_bank_;
    if (addr >= 0xE000U)
        bank = 15U;
    else if (addr >= 0xC000U)
        bank = 14U;
    else if (addr >= 0xA000U)
        bank = 13U;
    return prg_rom_[static_cast<std::size_t>(bank) * kBankSize + (addr & 0x1FFFU)];
}

void Mapper309::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        prg_ram_[addr & 0x1FFFU] = value;
        return;
    }
    if (addr >= 0x8000U && addr < 0xA000U)
        prg_bank_ = value & 0x0FU;
    else if (addr >= 0xE000U)
        mirror_ = (value & 0x08U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

bool Mapper309::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper309::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper309::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper309::mirror_mode() const {
    return mirror_;
}

void Mapper309::reset() {
    prg_bank_ = 0U;
    mirror_ = MirrorMode::Vertical;
}

void Mapper309::power_on() {
    reset();
    prg_ram_.fill(0U);
    chr_ram_.fill(0U);
}

void Mapper309::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(prg_bank_);
    writer.write<Byte>(mirror_ == MirrorMode::Horizontal ? 1U : 0U);
    writer.write_array(prg_ram_);
    writer.write_array(chr_ram_);
}

void Mapper309::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_bank = reader.read<Byte>();
    const auto mirror = reader.read<Byte>();
    std::array<Byte, kBankSize> prg_ram{};
    std::array<Byte, kBankSize> chr_ram{};
    reader.read_array(prg_ram);
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != kStateTag || prg_bank > 0x0FU || mirror > 1U) {
        reader.fail();
        return;
    }

    prg_bank_ = prg_bank;
    mirror_ = mirror != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    prg_ram_ = prg_ram;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
