#include "core/mappers/mapper392.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kGamePrgSize = 0x100000UZ;
constexpr std::size_t kMenuPrgSize = 0x008000UZ;
constexpr std::size_t kPrgSize = kGamePrgSize + kMenuPrgSize;
constexpr std::size_t kChrSize = 0x100000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::uint16_t kStateTag = 0x3920U;
constexpr Byte kOuterMask = 0x17U;
constexpr Byte kGameMode = 0x10U;

} // namespace

// Original implementation from NESdev mapper 392 oldid 19172. The article does
// not specify reset values; returning to the unlocked menu and clearing volatile
// CHR-RAM only on cold power are deterministic compatibility policies.
Mapper392::Mapper392(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), mirror_mode_(initial_mirror_) {
    power_on();
}

Result<void> Mapper392::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 392U) {
        return std::unexpected("Mapper 392 requires mapper ID 392");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 392 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != kChrSize) {
        return std::unexpected("Mapper 392 requires 1056 KiB PRG and 1 MiB CHR-ROM");
    }
    if (header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U || header.has_battery) {
        return std::unexpected("Mapper 392 requires only 8 KiB volatile CHR-RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 392 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

bool Mapper392::game_mode() const {
    return (outer_bank_ & kGameMode) != 0U;
}

unsigned Mapper392::inner_prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    if (slot == 1U) {
        return bank_registers_[7U] & 0x3FU;
    }
    if ((!prg_mode_ && slot == 0U) || (prg_mode_ && slot == 2U)) {
        return bank_registers_[6U] & 0x3FU;
    }
    return slot == 3U ? 15U : 14U;
}

Byte Mapper392::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    if (!game_mode()) {
        return prg_rom_[kGamePrgSize + (addr & 0x7FFFU)];
    }
    const unsigned bank = ((outer_bank_ & 7U) << 4U) | (inner_prg_bank(addr) & 0x0FU);
    const auto offset = bank * kPrgBankSize + (addr & 0x1FFFU);
    return prg_rom_[offset];
}

void Mapper392::write_prg(Address addr, Byte value) {
    if ((addr & 0xE000U) == 0x6000U) {
        if (!game_mode()) {
            outer_bank_ = value & kOuterMask;
        }
        return;
    }
    if (addr < 0x8000U) {
        return;
    }

    const bool even = (addr & 1U) == 0U;
    if (addr < 0xA000U) {
        if (even) {
            bank_select_ = value & 7U;
            prg_mode_ = (value & 0x40U) != 0U;
            chr_inversion_ = (value & 0x80U) != 0U;
        } else {
            bank_registers_[bank_select_] =
                bank_select_ >= 6U ? static_cast<Byte>(value & 0x3FU) : value;
        }
    } else if (addr < 0xC000U) {
        if (even) {
            mirror_mode_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        }
    } else if (addr < 0xE000U) {
        if (even) {
            irq_reload_ = value;
        } else {
            irq_reload_pending_ = true;
            irq_counter_ = 0U;
        }
    } else if (even) {
        irq_enabled_ = false;
        irq_pending_ = false;
    } else {
        irq_enabled_ = true;
    }
}

bool Mapper392::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper392::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

unsigned Mapper392::inner_chr_bank(Address addr) const {
    const unsigned slot = ((addr >> 10U) & 7U) ^ (chr_inversion_ ? 4U : 0U);
    if (slot < 4U) {
        return (bank_registers_[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return bank_registers_[slot - 2U];
}

Byte Mapper392::read_chr(Address addr) {
    if (!game_mode()) {
        return chr_ram_[addr & 0x1FFFU];
    }
    const unsigned bank = ((outer_bank_ & 7U) << 7U) | (inner_chr_bank(addr) & 0x7FU);
    const auto offset = bank * kChrBankSize + (addr & 0x3FFU);
    return chr_rom_[offset];
}

void Mapper392::write_chr(Address addr, Byte value) {
    if (!game_mode()) {
        chr_ram_[addr & 0x1FFFU] = value;
    }
}

MirrorMode Mapper392::mirror_mode() const {
    return mirror_mode_;
}

void Mapper392::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper392::reset() {
    outer_bank_ = 0U;
    bank_select_ = 0U;
    prg_mode_ = false;
    chr_inversion_ = false;
    bank_registers_.fill(0U);
    irq_counter_ = 0U;
    irq_reload_ = 0U;
    irq_reload_pending_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
    mirror_mode_ = initial_mirror_;
}

bool Mapper392::irq_pending() const {
    return irq_pending_;
}

void Mapper392::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper392::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_pending_) {
        irq_counter_ = irq_reload_;
        irq_reload_pending_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_) {
        irq_pending_ = true;
    }
}

std::uint16_t Mapper392::memory_tag() const {
    return static_cast<std::uint16_t>(initial_mirror_ == MirrorMode::Vertical ? 1U : 0U);
}

void Mapper392::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(memory_tag());
    writer.write(outer_bank_);
    writer.write(bank_select_);
    writer.write<Byte>((prg_mode_ ? 0x40U : 0U) | (chr_inversion_ ? 0x80U : 0U));
    writer.write_array(bank_registers_);
    writer.write<Byte>(static_cast<Byte>(mirror_mode_));
    writer.write(irq_counter_);
    writer.write(irq_reload_);
    writer.write<Byte>((irq_reload_pending_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write_array(chr_ram_);
}

void Mapper392::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto memory = reader.read<std::uint16_t>();
    const auto outer = reader.read<Byte>();
    const auto select = reader.read<Byte>();
    const auto mode = reader.read<Byte>();
    std::array<Byte, 8> banks{};
    reader.read_array(banks);
    const auto mirror = reader.read<Byte>();
    const auto counter = reader.read<Byte>();
    const auto reload = reader.read<Byte>();
    const auto irq = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || memory != memory_tag() || (outer & ~kOuterMask) != 0U ||
        select > 7U || (mode & 0x3FU) != 0U || banks[6U] > 0x3FU || banks[7U] > 0x3FU ||
        mirror > static_cast<Byte>(MirrorMode::Vertical) || irq > 7U ||
        ((irq & 4U) != 0U && (irq & 2U) == 0U)) {
        reader.fail();
        return;
    }

    outer_bank_ = outer;
    bank_select_ = select;
    prg_mode_ = (mode & 0x40U) != 0U;
    chr_inversion_ = (mode & 0x80U) != 0U;
    bank_registers_ = banks;
    mirror_mode_ = static_cast<MirrorMode>(mirror);
    irq_counter_ = counter;
    irq_reload_ = reload;
    irq_reload_pending_ = (irq & 1U) != 0U;
    irq_enabled_ = (irq & 2U) != 0U;
    irq_pending_ = (irq & 4U) != 0U;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
