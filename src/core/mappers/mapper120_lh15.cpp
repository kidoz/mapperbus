#include "core/mappers/mapper120_lh15.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x18000U;
constexpr std::size_t kFixedPrgOffset = 0x10000U;
constexpr uint16_t kStateTag = 0x7800U;

bool selects_prg_bank(Address addr) {
    return (addr & 0xE100U) == 0x4100U;
}

} // namespace

Lh15::Lh15(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {}

Result<void> Lh15::validate_geometry(const INesHeader& header,
                                     std::size_t prg_size,
                                     std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 120 supports submapper 0");
    if (prg_size != kPrgSize || chr_size != 0U)
        return std::unexpected("Mapper 120 requires 96 KiB PRG ROM and no CHR ROM");
    if (header.has_battery ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
                            header.chr_ram_size != 8192U || header.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 120 requires 8 KiB volatile CHR RAM and no other memory");
    if (header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 120 requires vertical mirroring");
    return {};
}

Byte Lh15::read_prg(Address addr) {
    if (addr >= 0x8000U)
        return prg_rom_[kFixedPrgOffset + (addr & 0x7FFFU)];
    if (addr >= 0x6000U)
        return prg_rom_[static_cast<std::size_t>(prg_bank_) * 8192U + (addr & 0x1FFFU)];
    return 0U;
}

void Lh15::write_prg(Address addr, Byte value) {
    write_expansion(addr, value);
}

bool Lh15::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Lh15::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Lh15::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Lh15::mirror_mode() const {
    return MirrorMode::Vertical;
}

void Lh15::reset() {
    prg_bank_ = 0U;
}

void Lh15::power_on() {
    reset();
    chr_ram_.fill(0U);
}

bool Lh15::maps_expansion(Address addr) const {
    return selects_prg_bank(addr);
}

Byte Lh15::expansion_read_mask(Address addr) const {
    return selects_prg_bank(addr) ? 0U : 0xFFU;
}

Byte Lh15::read_expansion(Address) {
    return 0U;
}

void Lh15::write_expansion(Address addr, Byte value) {
    if (selects_prg_bank(addr))
        prg_bank_ = value & 7U;
}

void Lh15::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(prg_bank_);
    writer.write_array(chr_ram_);
}

void Lh15::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_bank = reader.read<Byte>();
    std::array<Byte, 8192> chr_ram{};
    reader.read_array(chr_ram);
    if (!reader.ok() || tag != kStateTag || prg_bank > 7U) {
        reader.fail();
        return;
    }
    prg_bank_ = prg_bank;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
