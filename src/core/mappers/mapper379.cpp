#include "core/mappers/mapper379.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 0x8000UZ;
constexpr std::size_t kChrBankSize = 0x2000UZ;
constexpr std::size_t kMaxPrgSize = 4UZ * kPrgBankSize;
constexpr std::size_t kMaxChrSize = 4UZ * kChrBankSize;
constexpr std::uint16_t kStateTag = 0x7900U;
constexpr Byte kOuterBankMask = 0x0FU;

} // namespace

// Original implementation from NESdev mapper 379 oldid 24576. Reset selects
// bank zero as a deterministic policy; the article does not specify reset wiring.
Mapper379::Mapper379(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper379::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 379U) {
        return std::unexpected("Mapper 379 requires mapper ID 379");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 379 requires NES 2.0 submapper 0");
    }
    if (prg_size < kPrgBankSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U ||
        chr_size < kChrBankSize || chr_size > kMaxChrSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected(
            "Mapper 379 requires 32-128 KiB PRG and 8-32 KiB CHR ROM in bank units");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 379 supports ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 379 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper379::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    const auto offset =
        static_cast<std::size_t>(outer_bank_ & 3U) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper379::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
    outer_bank_ = value & kOuterBankMask;
}

bool Mapper379::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper379::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper379::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    const auto offset = static_cast<std::size_t>((outer_bank_ >> 2U) & 3U) * kChrBankSize +
                        (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper379::write_chr(Address, Byte) {}

MirrorMode Mapper379::mirror_mode() const {
    return initial_mirror_;
}

void Mapper379::power_on() {
    reset();
}

void Mapper379::reset() {
    outer_bank_ = 0U;
}

std::uint16_t Mapper379::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper379::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(outer_bank_);
}

void Mapper379::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto outer_bank = reader.read<Byte>();

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || (outer_bank & ~kOuterBankMask) != 0U) {
        reader.fail();
        return;
    }

    outer_bank_ = outer_bank;
}

} // namespace mapperbus::core
