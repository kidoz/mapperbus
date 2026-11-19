#include "core/mappers/mapper345.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kAxromBankSize = 0x8000UZ;
constexpr std::size_t kOuterBankSize = 0x20000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::size_t kMinPrgSize = kOuterBankSize;
constexpr std::size_t kMaxPrgSize = 4UZ * kOuterBankSize;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::uint16_t kStateTag = 0x3450U;

} // namespace

// Board decoding follows NESdev mapper 345 oldid 7895. Reset values and cold
// RAM clearing are deterministic policies because the board article does not
// specify them. The WRAM interface gates only the outer latch; no WRAM is populated.
Mapper345::Mapper345(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), initial_mirror_(header.mirror_mode),
      mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper345::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 345U) {
        return std::unexpected("Mapper 345 requires mapper ID 345");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 345 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        chr_size != 0U) {
        return std::unexpected(
            "Mapper 345 requires power-of-two 128-512 KiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 345 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 345 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper345::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }

    const std::size_t outer = static_cast<std::size_t>((outer_ >> 6U) & 3U) * kOuterBankSize;
    std::size_t inner = 0U;
    if ((outer_ & 0x0CU) == 0U) {
        inner = static_cast<std::size_t>(outer_ & 3U) * kAxromBankSize + (addr & 0x7FFFU);
    } else {
        const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
        const bool mode = (bank_select_ & 0x40U) != 0U;
        unsigned bank = 0U;
        switch (slot) {
        case 0U:
            bank = mode ? 14U : bank_registers_[6U] & 0x0FU;
            break;
        case 1U:
            bank = bank_registers_[7U] & 0x0FU;
            break;
        case 2U:
            bank = mode ? bank_registers_[6U] & 0x0FU : 14U;
            break;
        default:
            bank = 15U;
            break;
        }
        inner = bank * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    }
    return prg_rom_[(outer + inner) % prg_rom_.size()];
}

void Mapper345::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        if ((addr & 0xE000U) == 0x6000U && wram_enabled_ && !wram_write_protected_) {
            outer_ = value;
        }
        return;
    }

    switch (addr & 0xE001U) {
    case 0x8000U:
        bank_select_ = value & 0xC7U;
        break;
    case 0x8001U: {
        const unsigned index = bank_select_ & 7U;
        bank_registers_[index] = index >= 6U ? value & 0x3FU : value;
        break;
    }
    case 0xA000U:
        mirror_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
        wram_enabled_ = (value & 0x80U) != 0U;
        wram_write_protected_ = (value & 0x40U) != 0U;
        break;
    case 0xC000U:
        irq_latch_ = value;
        break;
    case 0xC001U:
        irq_counter_ = 0U;
        irq_reload_ = true;
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

bool Mapper345::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper345::prg_read_mask(Address addr) const {
    return addr >= 0x8000U ? 0xFFU : 0U;
}

Byte Mapper345::chr_bank(Address addr) const {
    const auto pattern_addr = static_cast<Address>(addr & 0x1FFFU);
    const unsigned slot = ((pattern_addr >> 10U) & 7U) ^ ((bank_select_ & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return static_cast<Byte>((bank_registers_[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return bank_registers_[slot - 2U];
}

Byte Mapper345::read_chr(Address addr) {
    const std::size_t offset =
        static_cast<std::size_t>(chr_bank(addr) & 7U) * kChrBankSize + (addr & (kChrBankSize - 1U));
    return chr_ram_[offset];
}

void Mapper345::write_chr(Address addr, Byte value) {
    const std::size_t offset =
        static_cast<std::size_t>(chr_bank(addr) & 7U) * kChrBankSize + (addr & (kChrBankSize - 1U));
    chr_ram_[offset] = value;
}

MirrorMode Mapper345::mirror_mode() const {
    if ((outer_ & 0x20U) != 0U) {
        return (outer_ & 0x10U) != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    }
    return mirror_;
}

std::optional<Address> Mapper345::nametable_address(Address addr) const {
    if ((outer_ & 0x20U) == 0U) {
        return std::nullopt;
    }
    return static_cast<Address>((static_cast<Address>(outer_ & 0x10U) << 6U) | (addr & 0x3FFU));
}

void Mapper345::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper345::reset() {
    outer_ = 0U;
    mirror_ = initial_mirror_;
    bank_select_ = 0U;
    bank_registers_.fill(0U);
    irq_counter_ = 0U;
    irq_latch_ = 0U;
    irq_reload_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
    wram_enabled_ = true;
    wram_write_protected_ = false;
}

bool Mapper345::irq_pending() const {
    return irq_pending_;
}

void Mapper345::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper345::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_) {
        irq_counter_ = irq_latch_;
        irq_reload_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_) {
        irq_pending_ = true;
    }
}

std::uint16_t Mapper345::config_state_tag() const {
    return initial_mirror_ == MirrorMode::Vertical ? 1U : 0U;
}

void Mapper345::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(config_state_tag());
    writer.write(outer_);
    writer.write(bank_select_);
    writer.write_array(bank_registers_);
    writer.write<Byte>(mirror_ == MirrorMode::Vertical ? 1U : 0U);
    writer.write(irq_counter_);
    writer.write(irq_latch_);
    writer.write<Byte>((irq_reload_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write<Byte>((wram_enabled_ ? 0x80U : 0U) | (wram_write_protected_ ? 0x40U : 0U));
    writer.write_array(chr_ram_);
}

void Mapper345::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto config_tag = reader.read<std::uint16_t>();
    const auto outer = reader.read<Byte>();
    const auto bank_select = reader.read<Byte>();
    std::array<Byte, 8> bank_registers{};
    reader.read_array(bank_registers);
    const auto mirror = reader.read<Byte>();
    const auto irq_counter = reader.read<Byte>();
    const auto irq_latch = reader.read<Byte>();
    const auto irq_flags = reader.read<Byte>();
    const auto ram_flags = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        config_tag != config_state_tag() || (bank_select & 0x38U) != 0U ||
        bank_registers[6U] > 0x3FU || bank_registers[7U] > 0x3FU || mirror > 1U || irq_flags > 7U ||
        ((irq_flags & 4U) != 0U && (irq_flags & 2U) == 0U) || (ram_flags & 0x3FU) != 0U) {
        reader.fail();
        return;
    }

    outer_ = outer;
    bank_select_ = bank_select;
    bank_registers_ = bank_registers;
    mirror_ = mirror != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    irq_counter_ = irq_counter;
    irq_latch_ = irq_latch;
    irq_reload_ = (irq_flags & 1U) != 0U;
    irq_enabled_ = (irq_flags & 2U) != 0U;
    irq_pending_ = (irq_flags & 4U) != 0U;
    wram_enabled_ = (ram_flags & 0x80U) != 0U;
    wram_write_protected_ = (ram_flags & 0x40U) != 0U;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
