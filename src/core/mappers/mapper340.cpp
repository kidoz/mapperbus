#include "core/mappers/mapper340.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kInnerBankSize = 0x4000UZ;
constexpr std::size_t kOuterBankSize = 0x20000UZ;
constexpr std::size_t kMinPrgSize = kOuterBankSize;
constexpr std::size_t kMaxPrgSize = 4UZ * kOuterBankSize;
constexpr std::uint16_t kStateTag = 0x3400U;

} // namespace

// Board decoding follows NESdev mapper 340 oldid 7874. Reset values are not
// documented; zeroed latches and volatile CHR RAM are deterministic policies.
Mapper340::Mapper340(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper340::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 340U) {
        return std::unexpected("Mapper 340 requires mapper ID 340");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 340 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        chr_size != 0U) {
        return std::unexpected(
            "Mapper 340 requires power-of-two 128-512 KiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0x2000U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 340 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 340 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper340::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    const std::size_t outer_bank = (address_latch_ >> 3U) & 3U;
    const bool nrom128 = (address_latch_ & 0x20U) != 0U;
    const std::size_t inner_bank = addr < 0xC000U || nrom128 ? inner_bank_ : 7U;
    const std::size_t offset =
        outer_bank * kOuterBankSize + inner_bank * kInnerBankSize + (addr & (kInnerBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper340::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U) {
        address_latch_ = addr & 0x3FU;
        inner_bank_ = value & 7U;
    }
}

Byte Mapper340::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper340::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper340::mirror_mode() const {
    return address_latch_ == 0x25U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper340::power_on() {
    chr_ram_.fill(0U);
    reset();
}

void Mapper340::reset() {
    address_latch_ = 0U;
    inner_bank_ = 0U;
}

void Mapper340::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(address_latch_);
    writer.write(inner_bank_);
    writer.write_array(chr_ram_);
}

void Mapper340::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto address_latch = reader.read<Byte>();
    const auto inner_bank = reader.read<Byte>();
    std::array<Byte, 0x2000> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() || address_latch > 0x3FU ||
        inner_bank > 7U) {
        reader.fail();
        return;
    }

    address_latch_ = address_latch;
    inner_bank_ = inner_bank;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
