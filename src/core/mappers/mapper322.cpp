#include "core/mappers/mapper322.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinRomSize = 128UZ * 1024UZ;
constexpr std::size_t kMaxRomSize = 2UZ * 1024UZ * 1024UZ;
constexpr std::size_t kPrgBankSize = 8192UZ;
constexpr std::size_t kNromBankSize = 16384UZ;
constexpr std::size_t kChrBankSize = 1024UZ;
constexpr std::uint16_t kStateTag = 0x3220U;

} // namespace

// Original implementation from NESdev mapper 322 r7827 and the MMC3 hardware
// reference. Reset latch values and mirroring of unconnected high ROM pins are
// deterministic policies; the board article does not specify them.
Mapper322::Mapper322(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), mirror_mode_(header.mirror_mode) {
    reset();
}

Result<void> Mapper322::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (!header.is_nes2 || header.mapper_number != 322U || header.submapper != 0U) {
        return std::unexpected("Mapper 322 requires NES 2.0 mapper 322 submapper 0");
    }
    if (prg_size < kMinRomSize || prg_size > kMaxRomSize || !std::has_single_bit(prg_size) ||
        chr_size < kMinRomSize || chr_size > kMaxRomSize || !std::has_single_bit(chr_size)) {
        return std::unexpected("Mapper 322 requires power-of-two 128 KiB-2 MiB PRG and CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 322 requires ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 322 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

unsigned Mapper322::outer_bank() const {
    return ((outer_ & 0x40U) >> 4U) | ((outer_ & 0x18U) >> 3U);
}

std::size_t Mapper322::outer_size() const {
    const bool large = (outer_ & 0xA0U) == 0xA0U;
    return (large ? 256UZ : 128UZ) * 1024UZ;
}

std::size_t Mapper322::prg_offset(Address addr) const {
    const auto outer = static_cast<std::size_t>(outer_bank());
    if ((outer_ & 0x20U) == 0U) {
        const unsigned inner = outer_ & 7U;
        const std::size_t base = outer * kMinRomSize + inner * kNromBankSize;
        if (inner == 0U || inner == 4U) {
            return base + (addr & 0x3FFFU);
        }
        return outer * kMinRomSize + (inner & ~1U) * kNromBankSize + (addr & 0x7FFFU);
    }

    const std::size_t size = outer_size();
    const std::size_t mask = size / kPrgBankSize - 1U;
    const unsigned slot = (addr - 0x8000U) >> 13U;
    std::size_t inner = 0U;
    if (slot == 1U) {
        inner = bank_registers_[7] & mask;
    } else if (slot == 3U) {
        inner = mask;
    } else if ((slot == 0U) == ((bank_select_ & 0x40U) == 0U)) {
        inner = bank_registers_[6] & mask;
    } else {
        inner = mask - 1U;
    }
    return outer * size + inner * kPrgBankSize + (addr & 0x1FFFU);
}

Byte Mapper322::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    return prg_rom_[prg_offset(addr) & (prg_rom_.size() - 1U)];
}

void Mapper322::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (prg_ram_enabled_) {
            outer_ = value;
        }
        return;
    }
    if (addr < 0x8000U) {
        return;
    }

    switch (addr & 0xE001U) {
    case 0x8000U:
        bank_select_ = value & 0xC7U;
        break;
    case 0x8001U: {
        const unsigned index = bank_select_ & 7U;
        bank_registers_[index] = index < 6U ? value : value & 0x3FU;
        break;
    }
    case 0xA000U:
        mirror_mode_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
        prg_ram_enabled_ = (value & 0x80U) != 0U;
        prg_ram_write_protected_ = (value & 0x40U) != 0U;
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

bool Mapper322::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper322::chr_bank(Address addr) const {
    unsigned slot = (addr >> 10U) & 7U;
    if ((bank_select_ & 0x80U) != 0U) {
        slot ^= 4U;
    }
    if (slot < 4U) {
        return (bank_registers_[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return bank_registers_[slot - 2U];
}

Byte Mapper322::read_chr(Address addr) {
    const std::size_t size = outer_size();
    const std::size_t mask = size / kChrBankSize - 1U;
    const std::size_t offset = static_cast<std::size_t>(outer_bank()) * size +
                               (chr_bank(addr) & mask) * kChrBankSize + (addr & 0x3FFU);
    return chr_rom_[offset & (chr_rom_.size() - 1U)];
}

void Mapper322::write_chr(Address, Byte) {}

MirrorMode Mapper322::mirror_mode() const {
    return mirror_mode_;
}

void Mapper322::power_on() {
    reset();
}

void Mapper322::reset() {
    outer_ = 0U;
    bank_select_ = 0U;
    bank_registers_.fill(0U);
    mirror_mode_ = initial_mirror_;
    irq_counter_ = 0U;
    irq_reload_ = 0U;
    irq_reload_flag_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
    prg_ram_enabled_ = true;
    prg_ram_write_protected_ = false;
}

bool Mapper322::irq_pending() const {
    return irq_pending_;
}

void Mapper322::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper322::clock_irq_counter() {
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

std::uint16_t Mapper322::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper322::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(outer_);
    writer.write(bank_select_);
    writer.write_array(bank_registers_);
    writer.write<Byte>(static_cast<Byte>(mirror_mode_));
    writer.write(irq_counter_);
    writer.write(irq_reload_);
    writer.write<Byte>((irq_reload_flag_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write<Byte>((prg_ram_enabled_ ? 0x80U : 0U) | (prg_ram_write_protected_ ? 0x40U : 0U));
}

void Mapper322::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto outer = reader.read<Byte>();
    const auto bank_select = reader.read<Byte>();
    std::array<Byte, 8> bank_registers{};
    reader.read_array(bank_registers);
    const auto mirror = reader.read<Byte>();
    const auto irq_counter = reader.read<Byte>();
    const auto irq_reload = reader.read<Byte>();
    const auto irq = reader.read<Byte>();
    const auto ram = reader.read<Byte>();

    const bool valid = reader.ok() && tag == state_tag() && prg_size == prg_rom_.size() &&
                       chr_size == chr_rom_.size() && (bank_select & 0x38U) == 0U &&
                       bank_registers[6] <= 0x3FU && bank_registers[7] <= 0x3FU &&
                       mirror <= static_cast<Byte>(MirrorMode::Vertical) && irq <= 7U &&
                       ((irq & 4U) == 0U || (irq & 2U) != 0U) && (ram & 0x3FU) == 0U;
    if (!valid) {
        reader.fail();
        return;
    }

    outer_ = outer;
    bank_select_ = bank_select;
    bank_registers_ = bank_registers;
    mirror_mode_ = static_cast<MirrorMode>(mirror);
    irq_counter_ = irq_counter;
    irq_reload_ = irq_reload;
    irq_reload_flag_ = (irq & 1U) != 0U;
    irq_enabled_ = (irq & 2U) != 0U;
    irq_pending_ = (irq & 4U) != 0U;
    prg_ram_enabled_ = (ram & 0x80U) != 0U;
    prg_ram_write_protected_ = (ram & 0x40U) != 0U;
}

} // namespace mapperbus::core
