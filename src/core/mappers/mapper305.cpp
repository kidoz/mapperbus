#include "core/mappers/mapper305.hpp"

#include <bit>
#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 2048UZ;
constexpr std::size_t kMinPrgSize = 16UZ * kPrgBankSize;
constexpr std::size_t kMaxPrgSize = 256UZ * kPrgBankSize;
constexpr uint16_t kStateTag = 0x0131U;

} // namespace

// Original implementation from NESdev mapper 305 revision 7786. Reset latch
// values, power-on RAM contents and disconnected high ROM pins are not measured;
// zeroed latches/RAM and power-of-two ROM wrapping are deterministic policies.
Mapper305::Mapper305(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper305::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (!header.is_nes2 || header.mapper_number != 305U || header.submapper != 0U)
        return std::unexpected("Mapper 305 requires NES 2.0 mapper 305 submapper 0");
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || !std::has_single_bit(prg_size))
        return std::unexpected("Mapper 305 requires power-of-two 32-512 KiB PRG ROM");
    if (chr_size != 0U)
        return std::unexpected("Mapper 305 requires 8 KiB CHR RAM and no CHR ROM");
    if (header.prg_ram_size != 0U || header.prg_nvram_size != 0U || header.chr_ram_size != 8192U ||
        header.chr_nvram_size != 0U)
        return std::unexpected("Mapper 305 requires only 8 KiB volatile CHR RAM");
    if (header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 305 requires hard-wired vertical mirroring");
    return {};
}

Byte Mapper305::read_prg(Address addr) {
    if (addr < 0x6000U)
        return 0U;

    std::size_t bank = 0U;
    if (addr < 0x8000U) {
        bank = prg_banks_[(addr - 0x6000U) >> 11U];
    } else {
        bank = 15U - ((addr - 0x8000U) >> 11U);
    }
    bank &= prg_rom_.size() / kPrgBankSize - 1U;
    return prg_rom_[bank * kPrgBankSize + (addr & 0x07FFU)];
}

void Mapper305::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    prg_banks_[(addr >> 11U) & 3U] = value;
}

bool Mapper305::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper305::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper305::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper305::mirror_mode() const {
    return MirrorMode::Vertical;
}

void Mapper305::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper305::reset() {
    prg_banks_.fill(0U);
}

void Mapper305::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write_array(prg_banks_);
    writer.write_array(chr_ram_);
}

void Mapper305::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    std::array<Byte, 4> prg_banks{};
    std::array<Byte, 8192> chr_ram{};
    reader.read_array(prg_banks);
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size()) {
        reader.fail();
        return;
    }

    prg_banks_ = prg_banks;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
