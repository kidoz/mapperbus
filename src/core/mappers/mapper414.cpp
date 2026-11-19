#include "core/mappers/mapper414.hpp"

#include <charconv>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x004000UZ;
constexpr std::size_t kMaxPrgSize = 0x040000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x010000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrBankSize = 0x002000UZ;
constexpr Address kControlMask = 0x20FFU;
constexpr std::uint16_t kStateTag = 0x4140U;

} // namespace

// Original implementation from NESdev mapper 414 oldid 24535. Electrical reset
// is unspecified: cleared latches are a deterministic compatibility policy.
// The solder-pad profile is explicit because the header cannot describe it.
Mapper414::Mapper414(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    power_on();
}

Result<void> Mapper414::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 414U) {
        return std::unexpected("Mapper 414 requires mapper ID 414");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 414 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U ||
        chr_size < kMinChrSize || chr_size > kMaxChrSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected(
            "Mapper 414 requires 16-256 KiB PRG and 8-64 KiB CHR ROM in bank units");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 414 provides no cartridge RAM or NVRAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 414 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 414 does not provide trainer RAM");
    }
    return {};
}

std::size_t Mapper414::prg_offset(Address addr) const {
    unsigned bank = (control_ & 6U) >> 1U;
    bank <<= 1U;
    if ((control_ & 0x2000U) != 0U) {
        bank |= (addr >> 14U) & 1U;
    }
    return static_cast<std::size_t>(bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

bool Mapper414::prg_rom_disabled(Address addr) const {
    if (addr < 0xC000U || (control_ & 0x80U) != 0U) {
        return false;
    }
    const unsigned selection = (control_ >> 3U) & 0x0FU;
    return (solder_pads_ & (1U << selection)) != 0U;
}

Byte Mapper414::read_prg(Address addr) {
    if (!maps_prg(addr) || prg_rom_disabled(addr) || prg_rom_.empty()) {
        return 0U;
    }
    return prg_rom_[prg_offset(addr) % prg_rom_.size()];
}

void Mapper414::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
    const Byte rom_value = prg_rom_disabled(addr) || prg_rom_.empty()
                               ? 0xFFU
                               : prg_rom_[prg_offset(addr) % prg_rom_.size()];
    control_ = addr & kControlMask;
    chr_bank_ = value & rom_value & 3U;
}

bool Mapper414::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper414::prg_read_mask(Address addr) const {
    return maps_prg(addr) && !prg_rom_disabled(addr) ? 0xFFU : 0U;
}

Byte Mapper414::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    const auto offset =
        static_cast<std::size_t>(chr_bank_) * kChrBankSize + (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper414::write_chr(Address, Byte) {}

MirrorMode Mapper414::mirror_mode() const {
    return (control_ & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

Result<void> Mapper414::configure_board_profile(std::string_view profile) {
    if (!profile.starts_with("solder-pads-")) {
        return std::unexpected("Mapper 414 profiles are solder-pads-0 through solder-pads-65535");
    }
    profile.remove_prefix(12U);
    unsigned value = 0U;
    const auto [end, error] =
        std::from_chars(profile.data(), profile.data() + profile.size(), value);
    if (error != std::errc{} || end != profile.data() + profile.size() || value > 0xFFFFU) {
        return std::unexpected("Mapper 414 profiles are solder-pads-0 through solder-pads-65535");
    }
    solder_pads_ = static_cast<std::uint16_t>(value);
    return {};
}

void Mapper414::power_on() {
    reset();
}

void Mapper414::reset() {
    control_ = 0U;
    chr_bank_ = 0U;
}

void Mapper414::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(solder_pads_);
    writer.write(control_);
    writer.write(chr_bank_);
}

void Mapper414::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto ram_size = reader.read<std::uint32_t>();
    const auto solder_pads = reader.read<std::uint16_t>();
    const auto control = reader.read<Address>();
    const auto chr_bank = reader.read<Byte>();

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || ram_size != 0U || solder_pads != solder_pads_ ||
        (control & ~kControlMask) != 0U || chr_bank > 3U) {
        reader.fail();
        return;
    }

    control_ = control;
    chr_bank_ = chr_bank;
}

} // namespace mapperbus::core
