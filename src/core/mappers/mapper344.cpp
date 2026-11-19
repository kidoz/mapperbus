#include "core/mappers/mapper344.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 8192UZ;
constexpr std::size_t kChrBankSize = 1024UZ;
constexpr std::size_t kInnerPrgSize = 131072UZ;
constexpr std::size_t kMinPrgSize = 32768UZ;
constexpr std::size_t kMaxPrgSize = 524288UZ;
constexpr std::size_t kMinChrSize = 8192UZ;
constexpr std::size_t kMaxChrSize = 524288UZ;
constexpr std::size_t kPrgRamSize = 8192UZ;
constexpr std::uint16_t kStateTag = 0x3440U;

} // namespace

// GN-26 decoding follows NESdev mapper 344 oldid 19185; ordinary MMC3 behavior
// follows MMC3 r24445. ROM bounds describe connected address pins. Reset defaults
// and cold RAM clearing are deterministic policies because the board article
// does not specify them. A connected menu pad is an explicit immutable option.
Mapper344::Mapper344(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper344::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 344U) {
        return std::unexpected("Mapper 344 requires mapper ID 344");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 344 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        !std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxChrSize) {
        return std::unexpected(
            "Mapper 344 requires power-of-two 32-512 KiB PRG and 8-512 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != kPrgRamSize || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 344 requires exactly 8 KiB volatile PRG RAM and no other RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 344 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

bool Mapper344::prg_rom_disabled() const {
    return disable_on_solder_test_ && (outer_ & 0x08U) != 0U;
}

Byte Mapper344::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        return prg_ram_enabled_ ? prg_ram_[addr & 0x1FFFU] : 0U;
    }
    if (addr < 0x8000U || prg_rom_disabled()) {
        return 0U;
    }

    std::size_t inner_offset = 0U;
    if ((outer_ & 0x04U) != 0U) {
        const std::size_t bank = (bank_registers_[6U] >> 1U) & 7U;
        inner_offset = bank * 2UZ * kPrgBankSize + (addr & 0x3FFFU);
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
        inner_offset = bank * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    }
    const std::size_t offset = static_cast<std::size_t>(outer_ & 3U) * kInnerPrgSize + inner_offset;
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper344::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (prg_ram_enabled_) {
            outer_ = value & 0x0FU;
            if (!prg_ram_write_protected_) {
                prg_ram_[addr & 0x1FFFU] = value;
            }
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
        bank_registers_[index] = index >= 6U ? value & 0x3FU : value;
        break;
    }
    case 0xA000U:
        mirror_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
        prg_ram_enabled_ = (value & 0x80U) != 0U;
        prg_ram_write_protected_ = (value & 0x40U) != 0U;
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

bool Mapper344::maps_prg(Address addr) const {
    if (addr >= 0x8000U) {
        return !prg_rom_disabled();
    }
    return addr >= 0x6000U && addr < 0x8000U && prg_ram_enabled_;
}

Byte Mapper344::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper344::chr_bank(Address addr) const {
    const auto pattern_addr = static_cast<Address>(addr & 0x1FFFU);
    const unsigned slot = ((pattern_addr >> 10U) & 7U) ^ ((bank_select_ & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return static_cast<Byte>((bank_registers_[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return bank_registers_[slot - 2U];
}

Byte Mapper344::read_chr(Address addr) {
    const Byte bank = chr_bank(addr);
    const std::size_t a18 = static_cast<std::size_t>((outer_ >> 1U) & 1U) << 18U;
    const std::size_t a17 = (outer_ & 0x04U) != 0U ? static_cast<std::size_t>(outer_ & 1U) << 17U
                                                   : static_cast<std::size_t>(bank & 0x80U) << 10U;
    const std::size_t offset = a18 | a17 | static_cast<std::size_t>(bank & 0x7FU) * kChrBankSize |
                               (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper344::write_chr(Address, Byte) {}

MirrorMode Mapper344::mirror_mode() const {
    return mirror_;
}

Result<void> Mapper344::configure_board_jumpers(Byte value) {
    if (value > 1U) {
        return std::unexpected(
            "Mapper 344 accepts 0 for an open menu pad or 1 for PRG disable on S");
    }
    disable_on_solder_test_ = value != 0U;
    return {};
}

void Mapper344::power_on() {
    reset();
    prg_ram_.fill(0U);
}

void Mapper344::reset() {
    outer_ = 0U;
    mirror_ = initial_mirror_;
    bank_select_ = 0U;
    bank_registers_.fill(0U);
    irq_counter_ = 0U;
    irq_latch_ = 0U;
    irq_reload_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
    prg_ram_enabled_ = true;
    prg_ram_write_protected_ = false;
}

bool Mapper344::irq_pending() const {
    return irq_pending_;
}

void Mapper344::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper344::clock_irq_counter() {
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

std::uint16_t Mapper344::config_state_tag() const {
    return static_cast<std::uint16_t>((initial_mirror_ == MirrorMode::Vertical ? 1U : 0U) |
                                      (disable_on_solder_test_ ? 2U : 0U));
}

void Mapper344::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(config_state_tag());
    writer.write(outer_);
    writer.write(bank_select_);
    writer.write_array(bank_registers_);
    writer.write<Byte>(mirror_ == MirrorMode::Vertical ? 1U : 0U);
    writer.write(irq_counter_);
    writer.write(irq_latch_);
    writer.write<Byte>((irq_reload_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write<Byte>((prg_ram_enabled_ ? 0x80U : 0U) | (prg_ram_write_protected_ ? 0x40U : 0U));
    writer.write_bytes(prg_ram_);
}

void Mapper344::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
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
    std::vector<Byte> prg_ram;
    reader.read_bytes(prg_ram, kPrgRamSize);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || config_tag != config_state_tag() || (outer & 0xF0U) != 0U ||
        (bank_select & 0x38U) != 0U || bank_registers[6U] > 0x3FU || bank_registers[7U] > 0x3FU ||
        mirror > 1U || irq_flags > 7U || ((irq_flags & 4U) != 0U && (irq_flags & 2U) == 0U) ||
        (ram_flags & 0x3FU) != 0U || prg_ram.size() != prg_ram_.size()) {
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
    prg_ram_enabled_ = (ram_flags & 0x80U) != 0U;
    prg_ram_write_protected_ = (ram_flags & 0x40U) != 0U;
    std::ranges::copy(prg_ram, prg_ram_.begin());
}

} // namespace mapperbus::core
