#include "core/mappers/mapper450.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x080000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::uint16_t kStateTag = 0x4500U;
constexpr Byte kOuterBankMask = 0x03U;
constexpr Byte kPrgBankMask = 0x1FU;

} // namespace

// Original implementation from NESdev mapper 450 oldid 19307 and VRC2/VRC4.
// Electrical reset values are unspecified; clearing all registers is a policy.
Mapper450::Mapper450(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper450::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 450U) {
        return std::unexpected("Mapper 450 requires mapper ID 450");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 450 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U ||
        chr_size < kMinChrSize || chr_size > kMaxChrSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected(
            "Mapper 450 requires 32-512 KiB PRG in 8 KiB units and 8-512 KiB CHR in 1 KiB units");
    }
    if (header.prg_ram_size != 0U || header.prg_nvram_size != 0U || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U || header.has_battery) {
        return std::unexpected("Mapper 450 has no PRG/CHR RAM, NVRAM or battery");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 450 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 450 does not provide trainer RAM");
    }
    return {};
}

bool Mapper450::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper450::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

std::size_t Mapper450::prg_offset(Address addr) const {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const unsigned inner = slot < 2U ? prg_banks_[slot] : 30U + slot - 2U;
    const unsigned bank = (inner & kPrgBankMask) | (outer_bank_ << 5U);
    return bank * kPrgBankSize + (addr & 0x1FFFU);
}

Byte Mapper450::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    return prg_rom_.empty() ? 0U : prg_rom_[prg_offset(addr) % prg_rom_.size()];
}

void Mapper450::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x7000U) {
        outer_bank_ = value & kOuterBankMask;
        return;
    }
    if (addr < 0x8000U) {
        return;
    }

    const unsigned group = addr >> 12U;
    const unsigned port = addr & 3U;
    if (group == 8U || group == 10U) {
        prg_banks_[group == 8U ? 0U : 1U] = value & kPrgBankMask;
    } else if (group == 9U) {
        mirror_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    } else if (group >= 11U && group <= 14U) {
        auto& bank = chr_banks_[(group - 11U) * 2U + (port >> 1U)];
        if ((port & 1U) == 0U) {
            bank = (bank & 0xF0U) | (value & 0x0FU);
        } else {
            bank = (bank & 0x0FU) | ((value & 0x0FU) << 4U);
        }
    }
}

std::size_t Mapper450::chr_offset(Address addr) const {
    const unsigned inner = chr_banks_[(addr >> 10U) & 7U];
    const unsigned bank = (inner & 0x7FU) | (outer_bank_ << 7U);
    return bank * kChrBankSize + (addr & 0x03FFU);
}

Byte Mapper450::read_chr(Address addr) {
    return chr_rom_.empty() ? 0U : chr_rom_[chr_offset(addr) % chr_rom_.size()];
}

void Mapper450::write_chr(Address, Byte) {}

MirrorMode Mapper450::mirror_mode() const {
    return mirror_;
}

void Mapper450::power_on() {
    reset();
}

void Mapper450::reset() {
    outer_bank_ = 0U;
    prg_banks_ = {0U, 1U};
    chr_banks_.fill(0U);
    mirror_ = initial_mirror_;
}

std::uint16_t Mapper450::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper450::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(outer_bank_);
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write(mirror_);
}

void Mapper450::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto outer_bank = reader.read<Byte>();
    std::array<Byte, 2> prg_banks{};
    std::array<Byte, 8> chr_banks{};
    reader.read_array(prg_banks);
    reader.read_array(chr_banks);
    const auto mirror = reader.read<MirrorMode>();

    const bool valid = reader.ok() && tag == state_tag() && prg_size == prg_rom_.size() &&
                       chr_size == chr_rom_.size() && outer_bank <= kOuterBankMask &&
                       prg_banks[0] <= kPrgBankMask && prg_banks[1] <= kPrgBankMask &&
                       mirror <= MirrorMode::Vertical;
    if (!valid) {
        reader.fail();
        return;
    }

    outer_bank_ = outer_bank;
    prg_banks_ = prg_banks;
    chr_banks_ = chr_banks;
    mirror_ = mirror;
}

} // namespace mapperbus::core
