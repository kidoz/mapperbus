#include "core/mappers/mapper412.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x080000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::uint16_t kStateTag = 0x4120U;

} // namespace

// Original implementation from NESdev mapper 412 oldid 21165. Reset restores
// the documented zero-valued outer registers as a deterministic policy. The
// WRAM interface gates only the outer registers; no WRAM is populated.
Mapper412::Mapper412(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), mirror_mode_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper412::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 412U) {
        return std::unexpected("Mapper 412 requires mapper ID 412");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 412 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U ||
        chr_size < kMinChrSize || chr_size > kMaxChrSize || chr_size % kChrBankSize != 0U) {
        return std::unexpected(
            "Mapper 412 requires 32-512 KiB PRG and 8-512 KiB CHR ROM in bank units");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 412 provides no cartridge RAM or NVRAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 412 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 412 does not provide trainer RAM");
    }
    return {};
}

unsigned Mapper412::mmc3_prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    return slot == 1U                  ? bank_registers_[7U] & 0x3FU
           : slot == 3U                ? 63U
           : (slot == 0U) == prg_mode_ ? 62U
                                       : bank_registers_[6U] & 0x3FU;
}

unsigned Mapper412::mapped_prg_bank(Address addr) const {
    const unsigned raw = mmc3_prg_bank(addr);
    unsigned bank = raw;

    if ((prg_bank_ & 2U) != 0U) {
        const unsigned a14 = (prg_bank_ & 4U) != 0U ? (addr >> 13U) & 2U : (prg_bank_ >> 2U) & 2U;
        bank = ((prg_bank_ & 0xF0U) >> 2U) | ((addr >> 13U) & 1U) | a14;
    } else {
        if ((mode_ & 0x02U) != 0U) {
            bank = (raw & ~0x20U) | ((mode_ & 0x04U) << 3U);
        }
    }

    if ((mode_ & 0x10U) != 0U) {
        bank = (bank & ~0x10U) | ((mode_ & 0x40U) >> 2U);
    }
    return bank;
}

Byte Mapper412::read_prg(Address addr) {
    if (!maps_prg(addr) || addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    const auto offset = static_cast<std::size_t>(mapped_prg_bank(addr)) * kPrgBankSize +
                        (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper412::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        if (!wram_enabled_ || wram_write_protected_) {
            return;
        }
        switch (addr & 0xE003U) {
        case 0x6000U:
            chr_bank_ = value & 0xFCU;
            break;
        case 0x6001U:
            if ((mode_ & 1U) == 0U) {
                mode_ = value;
            }
            break;
        case 0x6002U:
            prg_bank_ = value & 0xFEU;
            break;
        default:
            break;
        }
        return;
    }

    switch (addr & 0xE001U) {
    case 0x8000U:
        bank_select_ = value & 7U;
        prg_mode_ = (value & 0x40U) != 0U;
        chr_inversion_ = (value & 0x80U) != 0U;
        break;
    case 0x8001U:
        bank_registers_[bank_select_] =
            bank_select_ >= 6U ? static_cast<Byte>(value & 0x3FU) : value;
        break;
    case 0xA000U:
        mirror_mode_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
        wram_enabled_ = (value & 0x80U) != 0U;
        wram_write_protected_ = (value & 0x40U) != 0U;
        break;
    case 0xC000U:
        irq_reload_ = value;
        break;
    case 0xC001U:
        irq_counter_ = 0U;
        irq_reload_flag_ = true;
        break;
    case 0xE000U:
        irq_enabled_ = false;
        irq_pending_ = false;
        break;
    case 0xE001U:
        irq_enabled_ = true;
        break;
    default:
        break;
    }
}

bool Mapper412::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && (addr & 0xE003U) <= 0x6002U);
}

Byte Mapper412::prg_read_mask(Address addr) const {
    return addr >= 0x8000U ? 0xFFU : 0U;
}

Byte Mapper412::raw_chr_bank(Address addr) const {
    const unsigned slot = ((static_cast<unsigned>(addr) >> 10U) & 7U) ^ (chr_inversion_ ? 4U : 0U);
    if (slot < 4U) {
        return static_cast<Byte>((bank_registers_[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return bank_registers_[slot - 2U];
}

unsigned Mapper412::mapped_chr_bank(Address addr) const {
    unsigned bank = (prg_bank_ & 2U) != 0U ? ((chr_bank_ & 0xFCU) << 1U) | ((addr >> 10U) & 7U)
                                           : raw_chr_bank(addr);
    if ((mode_ & 0x20U) != 0U) {
        bank = (bank & ~0x80U) | (mode_ & 0x80U);
    }
    return (bank & ~0x100U) | ((mode_ & 0x08U) << 5U);
}

Byte Mapper412::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    const auto offset = static_cast<std::size_t>(mapped_chr_bank(addr)) * kChrBankSize +
                        (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper412::write_chr(Address, Byte) {}

MirrorMode Mapper412::mirror_mode() const {
    return mirror_mode_;
}

void Mapper412::power_on() {
    reset();
}

void Mapper412::reset() {
    chr_bank_ = 0U;
    mode_ = 0U;
    prg_bank_ = 0U;
    bank_select_ = 0U;
    prg_mode_ = false;
    chr_inversion_ = false;
    bank_registers_.fill(0U);
    irq_counter_ = 0U;
    irq_reload_ = 0U;
    irq_reload_flag_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
    wram_enabled_ = true;
    wram_write_protected_ = false;
    mirror_mode_ = initial_mirror_;
}

bool Mapper412::irq_pending() const {
    return irq_pending_;
}

void Mapper412::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper412::clock_irq_counter() {
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

std::uint16_t Mapper412::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper412::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(bank_select_);
    writer.write<Byte>((prg_mode_ ? 0x40U : 0U) | (chr_inversion_ ? 0x80U : 0U));
    writer.write_array(bank_registers_);
    writer.write(static_cast<Byte>(mirror_mode_));
    writer.write(irq_counter_);
    writer.write(irq_reload_);
    writer.write<Byte>((irq_reload_flag_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write<Byte>((wram_enabled_ ? 0x80U : 0U) | (wram_write_protected_ ? 0x40U : 0U));
    writer.write(chr_bank_);
    writer.write(mode_);
    writer.write(prg_bank_);
}

void Mapper412::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto chr_ram_size = reader.read<std::uint32_t>();
    const auto select = reader.read<Byte>();
    const auto mode = reader.read<Byte>();
    std::array<Byte, 8> banks{};
    reader.read_array(banks);
    const auto mirror = reader.read<Byte>();
    const auto counter = reader.read<Byte>();
    const auto reload = reader.read<Byte>();
    const auto irq = reader.read<Byte>();
    const auto ram = reader.read<Byte>();
    const auto chr_bank = reader.read<Byte>();
    const auto outer_mode = reader.read<Byte>();
    const auto prg_bank = reader.read<Byte>();

    const bool valid = reader.ok() && tag == state_tag() && prg_size == prg_rom_.size() &&
                       chr_size == chr_rom_.size() && chr_ram_size == 0U && select <= 7U &&
                       (mode & 0x3FU) == 0U && banks[6U] <= 63U && banks[7U] <= 63U &&
                       mirror <= static_cast<Byte>(MirrorMode::Vertical) && irq <= 7U &&
                       ((irq & 4U) == 0U || (irq & 2U) != 0U) && (ram & 0x3FU) == 0U &&
                       (chr_bank & 3U) == 0U && (prg_bank & 1U) == 0U;
    if (!valid) {
        reader.fail();
        return;
    }

    bank_select_ = select;
    prg_mode_ = (mode & 0x40U) != 0U;
    chr_inversion_ = (mode & 0x80U) != 0U;
    bank_registers_ = banks;
    mirror_mode_ = static_cast<MirrorMode>(mirror);
    irq_counter_ = counter;
    irq_reload_ = reload;
    irq_reload_flag_ = (irq & 1U) != 0U;
    irq_enabled_ = (irq & 2U) != 0U;
    irq_pending_ = (irq & 4U) != 0U;
    wram_enabled_ = (ram & 0x80U) != 0U;
    wram_write_protected_ = (ram & 0x40U) != 0U;
    chr_bank_ = chr_bank;
    mode_ = outer_mode;
    prg_bank_ = prg_bank;
}

} // namespace mapperbus::core
