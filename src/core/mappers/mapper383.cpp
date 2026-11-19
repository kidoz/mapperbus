#include "core/mappers/mapper383.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::size_t kMinPrgSize = 0x8000UZ;
constexpr std::size_t kMaxPrgSize = 0x80000UZ;
constexpr std::size_t kMinChrSize = 0x2000UZ;
constexpr std::size_t kMaxChrSize = 0x80000UZ;
constexpr std::uint16_t kStateTag = 0x3830U;

} // namespace

// Original implementation from NESdev mapper 383 oldid 7997. Reset is not
// specified by the article; zeroed PAL/MMC3 latches and header CIRAM are the
// deterministic compatibility policy. The board has no cartridge RAM or audio.
Mapper383::Mapper383(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    reset();
}

Result<void> Mapper383::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 383U) {
        return std::unexpected("Mapper 383 requires mapper ID 383");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 383 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U ||
        chr_size < kMinChrSize || chr_size > kMaxChrSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected(
            "Mapper 383 requires 32-512 KiB PRG and 8-512 KiB CHR ROM in bank units");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 383 supports ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 383 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Address Mapper383::clone_address(Address addr) const {
    if (pal_outer_ != 3U) {
        return addr;
    }
    switch (addr & 0xE000U) {
    case 0x6000U:
    case 0xA000U:
        return 0xE000U;
    case 0x8000U:
        return 0xC000U;
    case 0xC000U:
        return 0x8000U;
    case 0xE000U:
        return 0xA000U;
    default:
        return addr;
    }
}

Byte Mapper383::raw_prg_bank(Address clone_addr) const {
    const bool prg_mode = (bank_select_ & 0x40U) != 0U;
    switch ((clone_addr >> 13U) & 3U) {
    case 0U:
        return prg_mode ? 14U : bank_registers_[6U];
    case 1U:
        return bank_registers_[7U];
    case 2U:
        return prg_mode ? bank_registers_[6U] : 14U;
    default:
        return 15U;
    }
}

Byte Mapper383::raw_chr_bank(Address addr) const {
    const auto slot =
        static_cast<unsigned>(((addr >> 10U) & 7U) ^ ((bank_select_ & 0x80U) != 0U ? 4U : 0U));
    if (slot < 4U) {
        return static_cast<Byte>((bank_registers_[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return bank_registers_[slot - 2U];
}

Byte Mapper383::read_prg(Address addr) {
    const bool mode_three_low_window = addr >= 0x6000U && addr < 0x8000U && pal_outer_ == 3U;
    if ((addr < 0x8000U && !mode_three_low_window) || prg_rom_.empty()) {
        return 0U;
    }

    const auto clone_addr = clone_address(addr);
    const auto raw_bank = raw_prg_bank(clone_addr);
    unsigned mask = 0x0FU;
    unsigned outer = static_cast<unsigned>(pal_outer_) << 4U;
    if (pal_outer_ == 0U) {
        mask = pal_prg_a16_ ? 0x07U : 0x03U;
        outer = (pal_prg_a15_ ? 0x08U : 0U) | (pal_prg_a16_ ? 0x04U : 0U);
    } else if (pal_outer_ == 3U && addr < 0x8000U) {
        mask = 0x0BU;
    }
    const auto bank = static_cast<std::size_t>((raw_bank & mask) | outer);
    const auto offset = bank * kPrgBankSize + (addr & 0x1FFFU);
    const Byte result = prg_rom_[offset % prg_rom_.size()];

    if (pal_outer_ == 0U && addr >= 0x8000U && addr < 0xC000U) {
        pal_prg_a16_ = (raw_bank & 0x04U) != 0U;
    }
    return result;
}

void Mapper383::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }

    if ((addr & 0x8100U) == 0x8100U) {
        pal_prg_a15_ = (addr & 0x2000U) != 0U;
        pal_outer_ = static_cast<Byte>((addr >> 4U) & 3U);
    }

    const bool even = (addr & 1U) == 0U;
    if (addr < 0xA000U) {
        if (even) {
            bank_select_ = value & 0xC7U;
        } else if ((bank_select_ & 7U) < 6U) {
            bank_registers_[bank_select_ & 7U] = value;
        } else {
            bank_registers_[bank_select_ & 7U] = value & 0x3FU;
        }
    } else if (addr < 0xC000U) {
        if (even) {
            mirror_mode_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        }
    } else if (addr < 0xE000U) {
        if (even) {
            irq_reload_ = value;
        } else {
            irq_reload_flag_ = true;
            irq_counter_ = 0U;
        }
    } else if (even) {
        irq_enabled_ = false;
        irq_pending_ = false;
    } else {
        irq_enabled_ = true;
    }
}

bool Mapper383::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && addr < 0x8000U && pal_outer_ == 3U);
}

Byte Mapper383::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper383::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    const auto bank =
        static_cast<std::size_t>(raw_chr_bank(addr) | (static_cast<unsigned>(pal_outer_) << 7U));
    const auto offset = bank * kChrBankSize + (addr & 0x03FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper383::write_chr(Address, Byte) {}

MirrorMode Mapper383::mirror_mode() const {
    return mirror_mode_;
}

void Mapper383::reset() {
    bank_select_ = 0U;
    bank_registers_.fill(0U);
    mirror_mode_ = initial_mirror_;
    irq_counter_ = 0U;
    irq_reload_ = 0U;
    irq_reload_flag_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
    pal_prg_a15_ = false;
    pal_prg_a16_ = false;
    pal_outer_ = 0U;
}

bool Mapper383::irq_pending() const {
    return irq_pending_;
}

void Mapper383::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper383::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_flag_) {
        irq_counter_ = irq_reload_;
        irq_reload_flag_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_) {
        irq_pending_ = true;
    }
}

std::uint16_t Mapper383::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper383::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(bank_select_);
    writer.write_array(bank_registers_);
    writer.write(static_cast<Byte>(mirror_mode_));
    writer.write(irq_counter_);
    writer.write(irq_reload_);
    writer.write<Byte>((irq_reload_flag_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write<Byte>(pal_prg_a15_ ? 1U : 0U);
    writer.write<Byte>(pal_prg_a16_ ? 1U : 0U);
    writer.write(pal_outer_);
}

void Mapper383::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto bank_select = reader.read<Byte>();
    std::array<Byte, 8> bank_registers{};
    reader.read_array(bank_registers);
    const auto mirror = reader.read<Byte>();
    const auto irq_counter = reader.read<Byte>();
    const auto irq_reload = reader.read<Byte>();
    const auto irq = reader.read<Byte>();
    const auto pal_a15 = reader.read<Byte>();
    const auto pal_a16 = reader.read<Byte>();
    const auto pal_outer = reader.read<Byte>();

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || (bank_select & 0x38U) != 0U || bank_registers[6U] > 0x3FU ||
        bank_registers[7U] > 0x3FU || mirror > static_cast<Byte>(MirrorMode::Vertical) ||
        irq > 7U || ((irq & 4U) != 0U && (irq & 2U) == 0U) || pal_a15 > 1U || pal_a16 > 1U ||
        pal_outer > 3U) {
        reader.fail();
        return;
    }

    bank_select_ = bank_select;
    bank_registers_ = bank_registers;
    mirror_mode_ = static_cast<MirrorMode>(mirror);
    irq_counter_ = irq_counter;
    irq_reload_ = irq_reload;
    irq_reload_flag_ = (irq & 1U) != 0U;
    irq_enabled_ = (irq & 2U) != 0U;
    irq_pending_ = (irq & 4U) != 0U;
    pal_prg_a15_ = pal_a15 != 0U;
    pal_prg_a16_ = pal_a16 != 0U;
    pal_outer_ = pal_outer;
}

} // namespace mapperbus::core
