#include "core/mappers/mapper314.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankBytes = 16384U;
constexpr std::size_t kChrBankBytes = 8192U;
constexpr std::size_t kMaxPrgBytes = 4U * 1024U * 1024U;
constexpr std::size_t kMaxChrRomBytes = 64U * kChrBankBytes;
constexpr std::size_t kMaxChrRamBytes = 4U * kChrBankBytes;
constexpr std::uint16_t kStateTag = 0x3140U;

} // namespace

// Original implementation from NESdev mapper 314 revision 23247. Its revision
// history records a 32 KiB CHR-RAM population in addition to the original 8 KiB
// variant. The low-port masks are unknown, so only literal $5000-$5002 decode.
Mapper314::Mapper314(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), chr_ram_(header.chr_ram_size) {
    power_on();
}

Result<void> Mapper314::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (!header.is_nes2 || header.mapper_number != 314U || header.submapper != 0U) {
        return std::unexpected("Mapper 314 requires NES 2.0 mapper 314 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < 2U * kPrgBankBytes ||
        prg_size > kMaxPrgBytes) {
        return std::unexpected("Mapper 314 requires power-of-two 32 KiB-4 MiB PRG ROM");
    }
    if (chr_size != 0U && (!std::has_single_bit(chr_size) || chr_size < kChrBankBytes ||
                           chr_size > kMaxChrRomBytes)) {
        return std::unexpected("Mapper 314 requires power-of-two 8-512 KiB CHR ROM");
    }
    const bool valid_chr_ram = chr_size == 0U && (header.chr_ram_size == kChrBankBytes ||
                                                  header.chr_ram_size == kMaxChrRamBytes);
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_nvram_size != 0U ||
        (chr_size == 0U ? !valid_chr_ram : header.chr_ram_size != 0U)) {
        return std::unexpected(
            "Mapper 314 requires 8 or 32 KiB volatile CHR RAM only without CHR ROM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 314 requires ordinary H/V nametable metadata");
    }
    return {};
}

Byte Mapper314::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }

    const std::size_t outer_bank = static_cast<std::size_t>(miscellaneous_ & 1U) << 7U;
    const std::size_t base_bank = outer_bank | (static_cast<std::size_t>(prg_bank_ & 0x3FU) << 1U);
    std::size_t bank = base_bank;
    if ((miscellaneous_ & 0x20U) == 0U) {
        bank =
            (base_bank & ~7UZ) | (addr >= 0xC000U ? 7UZ : static_cast<std::size_t>(unrom_latch_));
    } else if ((prg_bank_ & 0x80U) != 0U) {
        bank = (base_bank & ~1UZ) | ((addr >> 14U) & 1U);
    } else {
        bank |= (prg_bank_ >> 6U) & 1U;
    }
    const std::size_t offset = bank * kPrgBankBytes + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper314::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U) {
        unrom_latch_ = value & 7U;
    }
}

Byte Mapper314::read_chr(Address addr) {
    const std::size_t bank =
        (static_cast<std::size_t>(chr_bank_) << 2U) | ((miscellaneous_ >> 1U) & 3U);
    const std::size_t offset = bank * kChrBankBytes + (addr & 0x1FFFU);
    if (!chr_ram_.empty()) {
        return chr_ram_[offset % chr_ram_.size()];
    }
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper314::write_chr(Address addr, Byte value) {
    if (!chr_ram_.empty()) {
        const std::size_t bank =
            (static_cast<std::size_t>(chr_bank_) << 2U) | ((miscellaneous_ >> 1U) & 3U);
        const std::size_t offset = bank * kChrBankBytes + (addr & 0x1FFFU);
        chr_ram_[offset % chr_ram_.size()] = value;
    }
}

MirrorMode Mapper314::mirror_mode() const {
    return (miscellaneous_ & 0x20U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper314::write_expansion(Address addr, Byte value) {
    switch (addr) {
    case 0x5000U:
        miscellaneous_ = value;
        break;
    case 0x5001U:
        prg_bank_ = value;
        break;
    case 0x5002U:
        chr_bank_ = value & 0x0FU;
        break;
    default:
        break;
    }
}

void Mapper314::power_on() {
    miscellaneous_ = 0x80U;
    prg_bank_ = 0x43U;
    chr_bank_ = 0U;
    unrom_latch_ = 0U;
    std::ranges::fill(chr_ram_, 0U);
}

void Mapper314::reset() {
    // Reset behavior is undocumented; retain all latches and volatile CHR RAM.
}

std::uint16_t Mapper314::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag | (!chr_ram_.empty() ? 1U : 0U) |
                                      (chr_ram_.size() == kMaxChrRamBytes ? 2U : 0U));
}

void Mapper314::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(miscellaneous_);
    writer.write(prg_bank_);
    writer.write(chr_bank_);
    writer.write(unrom_latch_);
    if (!chr_ram_.empty()) {
        writer.write_bytes(chr_ram_);
    }
}

void Mapper314::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto miscellaneous = reader.read<Byte>();
    const auto prg_bank = reader.read<Byte>();
    const auto chr_bank = reader.read<Byte>();
    const auto unrom_latch = reader.read<Byte>();
    std::vector<Byte> chr_ram;
    if (!chr_ram_.empty()) {
        reader.read_bytes(chr_ram, kMaxChrRamBytes);
    }

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || chr_bank > 0x0FU || unrom_latch > 7U ||
        chr_ram.size() != chr_ram_.size()) {
        reader.fail();
        return;
    }

    miscellaneous_ = miscellaneous;
    prg_bank_ = prg_bank;
    chr_bank_ = chr_bank;
    unrom_latch_ = unrom_latch;
    chr_ram_ = std::move(chr_ram);
}

} // namespace mapperbus::core
