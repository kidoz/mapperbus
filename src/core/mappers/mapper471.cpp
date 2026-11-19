#include "core/mappers/mapper471.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x010000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x004000UZ;
constexpr std::size_t kPrgBankSize = 0x008000UZ;
constexpr std::size_t kChrBankSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x4710U;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 471 oldid 23357.
// Reset clears the latch, pin history and IRQ as a deterministic policy.
Mapper471::Mapper471(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    reset();
}

Result<void> Mapper471::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 471U) {
        return std::unexpected("Mapper 471 requires mapper ID 471");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 471 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        !std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxChrSize) {
        return std::unexpected(
            "Mapper 471 requires power-of-two 32-64 KiB PRG and 8-16 KiB CHR ROM");
    }
    if (header.prg_ram_size != 0U || header.prg_nvram_size != 0U || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U || header.has_battery) {
        return std::unexpected("Mapper 471 has no cartridge RAM or nonvolatile storage");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 471 requires hard-wired H/V CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 471 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper471::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    const std::size_t offset =
        static_cast<std::size_t>(registers_.bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper471::write_prg(Address addr, Byte /*value*/) {
    if (addr < 0x8000U) {
        return;
    }
    registers_.bank = addr & 1U;
    registers_.irq_pending = false;
}

bool Mapper471::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper471::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper471::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    const std::size_t offset =
        static_cast<std::size_t>(registers_.bank) * kChrBankSize + (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper471::write_chr(Address /*addr*/, Byte /*value*/) {}

MirrorMode Mapper471::mirror_mode() const {
    return initial_mirror_;
}

void Mapper471::reset() {
    registers_ = {};
}

bool Mapper471::irq_pending() const {
    return registers_.irq_pending;
}

void Mapper471::acknowledge_irq() {
    registers_.irq_pending = false;
}

void Mapper471::observe_ppu_a12(bool high) {
    if (!high && registers_.a12_high) {
        registers_.irq_pending = true;
    }
    registers_.a12_high = high;
}

std::uint16_t Mapper471::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper471::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    const Byte flags = static_cast<Byte>(registers_.bank | (registers_.a12_high ? 2U : 0U) |
                                         (registers_.irq_pending ? 4U : 0U));
    writer.write(flags);
}

void Mapper471::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto flags = reader.read<Byte>();

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || flags > 7U) {
        reader.fail();
        return;
    }

    Registers next{};
    next.bank = flags & 1U;
    next.a12_high = (flags & 2U) != 0U;
    next.irq_pending = (flags & 4U) != 0U;
    registers_ = next;
}

} // namespace mapperbus::core
