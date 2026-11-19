#include "core/mappers/mapper527.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x040000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x040000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::uint16_t kStateTag = 0x5270U;
constexpr Byte kPrgBankMask = 0x1FU;
constexpr Byte kChrNibbleMask = 0x0FU;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 527 oldid 8124.
// CHR A17 also drives CIRAM A10, so the first two CHR selectors control both
// nametable pairs. Reset values are unspecified: use deterministic VRC defaults.
Mapper527::Mapper527(const INesHeader& /*header*/,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> Mapper527::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 527U) {
        return std::unexpected("Mapper 527 requires mapper ID 527");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 527 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        !std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxChrSize) {
        return std::unexpected(
            "Mapper 527 requires power-of-two 32-256 KiB PRG and 8-256 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 527 provides no cartridge RAM or NVRAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 527 requires ordinary H/V CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 527 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper527::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x7000U) {
        return state_.microwire;
    }
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }

    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    const std::size_t count = prg_rom_.size() / kPrgBankSize;
    std::size_t bank = count - 1U;
    if (slot == 0U) {
        bank = state_.prg_banks[0U];
    } else if (slot == 1U) {
        bank = state_.prg_banks[1U];
    } else if (slot == 2U) {
        bank = count - 2U;
    }
    const std::size_t offset = bank * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper527::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x7000U) {
        state_.microwire = value & 1U;
        return;
    }
    if (addr < 0x8000U) {
        return;
    }

    const auto group = static_cast<unsigned>(addr) >> 12U;
    const auto port = static_cast<unsigned>(addr) & 3U;
    if (group == 8U || group == 10U) {
        state_.prg_banks[group == 8U ? 0U : 1U] = value & kPrgBankMask;
    } else if (group >= 11U && group <= 14U) {
        auto& bank = state_.chr_banks[(group - 11U) * 2U + (port >> 1U)];
        if ((port & 1U) == 0U) {
            bank = (bank & 0xF0U) | (value & kChrNibbleMask);
        } else {
            bank = (bank & kChrNibbleMask) | ((value & kChrNibbleMask) << 4U);
        }
    }
}

bool Mapper527::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && addr < 0x7000U);
}

Byte Mapper527::prg_read_mask(Address addr) const {
    if (addr >= 0x8000U) {
        return 0xFFU;
    }
    return addr >= 0x6000U && addr < 0x7000U ? 0x01U : 0x00U;
}

Byte Mapper527::read_chr(Address addr) {
    if (addr >= 0x2000U || chr_rom_.empty()) {
        return 0U;
    }
    const auto bank = static_cast<std::size_t>(state_.chr_banks[(addr >> 10U) & 7U]);
    const std::size_t offset = bank * kChrBankSize + (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper527::write_chr(Address /*addr*/, Byte /*value*/) {}

MirrorMode Mapper527::mirror_mode() const {
    const bool first = (state_.chr_banks[0U] & 0x80U) != 0U;
    const bool second = (state_.chr_banks[1U] & 0x80U) != 0U;
    if (first == second) {
        return first ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    }
    return first ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

std::optional<Address> Mapper527::nametable_address(Address addr) const {
    const auto pair = (static_cast<unsigned>(addr) >> 11U) & 1U;
    const auto page = (state_.chr_banks[pair] & 0x80U) != 0U ? 0x400U : 0U;
    return static_cast<Address>(page | (addr & 0x3FFU));
}

void Mapper527::reset() {
    state_ = State{};
}

void Mapper527::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write_array(state_.prg_banks);
    writer.write_array(state_.chr_banks);
    writer.write(state_.microwire);
}

void Mapper527::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    State next;
    reader.read_array(next.prg_banks);
    reader.read_array(next.chr_banks);
    next.microwire = reader.read<Byte>();

    if (!reader.ok() || reader.remaining() != 0U || tag != kStateTag ||
        prg_size != prg_rom_.size() || chr_size != chr_rom_.size() ||
        next.prg_banks[0U] > kPrgBankMask || next.prg_banks[1U] > kPrgBankMask ||
        next.microwire > 1U) {
        reader.fail();
        return;
    }

    state_ = next;
}

} // namespace mapperbus::core
