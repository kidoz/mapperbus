#include "core/mappers/mapper525.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x004000UZ;
constexpr std::size_t kMaxPrgSize = 0x020000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x040000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::uint16_t kStateTag = 0x5250U;
constexpr Byte kPrgBankMask = 0x07U;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 525 oldid 8120.
// Reset values are unspecified: clear both bank files and restore header CIRAM.
Mapper525::Mapper525(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    reset();
}

Result<void> Mapper525::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 525U) {
        return std::unexpected("Mapper 525 requires mapper ID 525");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 525 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        !std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxChrSize) {
        return std::unexpected(
            "Mapper 525 requires power-of-two 16-128 KiB PRG and 8-256 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 525 provides no cartridge RAM or NVRAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 525 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 525 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper525::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    if (addr >= 0xC000U) {
        return prg_rom_[prg_rom_.size() - kPrgBankSize + (addr & (kPrgBankSize - 1U))];
    }
    const auto offset =
        static_cast<std::size_t>(state_.prg_bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper525::write_prg(Address addr, Byte value) {
    switch (addr & 0xF000U) {
    case 0x8000U:
        state_.prg_bank = static_cast<Byte>((static_cast<unsigned>(value) >> 1U) & kPrgBankMask);
        break;
    case 0x9000U:
        state_.mirror = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xB000U:
        state_.chr_banks[addr & 7U] = value;
        break;
    default:
        break;
    }
}

bool Mapper525::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper525::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper525::read_chr(Address addr) {
    if (addr >= 0x2000U || chr_rom_.empty()) {
        return 0U;
    }
    const auto bank =
        static_cast<std::size_t>(state_.chr_banks[(static_cast<unsigned>(addr) >> 10U) & 7U]);
    const std::size_t offset = bank * kChrBankSize + (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper525::write_chr(Address /*addr*/, Byte /*value*/) {}

MirrorMode Mapper525::mirror_mode() const {
    return state_.mirror;
}

void Mapper525::reset() {
    state_ = State{};
    state_.mirror = initial_mirror_;
}

std::uint16_t Mapper525::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper525::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(state_.prg_bank);
    writer.write_array(state_.chr_banks);
    writer.write<Byte>(state_.mirror == MirrorMode::Horizontal ? 1U : 0U);
}

void Mapper525::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    State next;
    next.prg_bank = reader.read<Byte>();
    reader.read_array(next.chr_banks);
    const auto mirror = reader.read<Byte>();

    if (!reader.ok() || reader.remaining() != 0U || tag != state_tag() ||
        prg_size != prg_rom_.size() || chr_size != chr_rom_.size() ||
        next.prg_bank > kPrgBankMask || mirror > 1U) {
        reader.fail();
        return;
    }

    next.mirror = mirror == 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    state_ = next;
}

} // namespace mapperbus::core
