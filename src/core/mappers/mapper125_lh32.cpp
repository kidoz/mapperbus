#include "core/mappers/mapper125_lh32.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kBankSize = 8192U;
constexpr std::size_t kPrgSize = 16U * kBankSize;
constexpr uint16_t kStateTag = 0x7D00U;

} // namespace

Lh32::Lh32(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Lh32::validate_geometry(const INesHeader& header,
                                     std::size_t prg_size,
                                     std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 125 supports submapper 0");
    if (prg_size != kPrgSize || chr_size != 0U)
        return std::unexpected("Mapper 125 requires 128 KiB PRG ROM and no CHR ROM");
    if (header.has_battery ||
        (header.is_nes2 && (header.prg_ram_size != 8192U || header.prg_nvram_size != 0U ||
                            header.chr_ram_size != 8192U || header.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 125 requires 8 KiB volatile PRG RAM and 8 KiB CHR RAM");
    if (header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 125 requires vertical mirroring");
    return {};
}

Byte Lh32::read_prg(Address addr) {
    if (addr < 0x6000U)
        return 0U;
    if (addr < 0x8000U)
        return prg_rom_[static_cast<std::size_t>(prg_bank_) * kBankSize + (addr & 0x1FFFU)];
    if (addr < 0xC000U) {
        const auto bank = 12U + ((addr - 0x8000U) >> 13U);
        return prg_rom_[bank * kBankSize + (addr & 0x1FFFU)];
    }
    if (addr < 0xE000U)
        return prg_ram_[addr & 0x1FFFU];
    return prg_rom_[15U * kBankSize + (addr & 0x1FFFU)];
}

void Lh32::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U)
        prg_bank_ = value & 0x0FU;
    else if (addr >= 0xC000U && addr < 0xE000U)
        prg_ram_[addr & 0x1FFFU] = value;
}

bool Lh32::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Lh32::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Lh32::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Lh32::mirror_mode() const {
    return MirrorMode::Vertical;
}

void Lh32::reset() {
    prg_bank_ = 0U;
}

void Lh32::power_on() {
    reset();
    prg_ram_.fill(0U);
    chr_ram_.fill(0U);
}

void Lh32::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(prg_bank_);
    writer.write_array(prg_ram_);
    writer.write_array(chr_ram_);
}

void Lh32::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_bank = reader.read<Byte>();
    std::array<Byte, 8192> prg_ram{};
    std::array<Byte, 8192> chr_ram{};
    reader.read_array(prg_ram);
    reader.read_array(chr_ram);
    if (!reader.ok() || tag != kStateTag || prg_bank > 15U) {
        reader.fail();
        return;
    }
    prg_bank_ = prg_bank;
    prg_ram_ = prg_ram;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
