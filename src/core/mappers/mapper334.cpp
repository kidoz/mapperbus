#include "core/mappers/mapper334.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 32768U;
constexpr std::size_t kChrBankSize = 1024U;
constexpr std::size_t kMinPrgSize = 32768U;
constexpr std::size_t kMaxPrgSize = 128U * 1024U;
constexpr std::size_t kMinChrSize = 8192U;
constexpr std::size_t kMaxChrSize = 256U * 1024U;
constexpr std::uint16_t kStateTag = 0x3340U;

} // namespace

// Board decoding follows NESdev mapper 334 r7857 and ordinary MMC3 behavior.
// The article specifies no reset values, unknown-register function, IRQ revision,
// or smaller ROM populations. Zeroed registers, header mirroring, repeated
// zero-latch IRQs, and wrapping after pin decoding are deterministic policies.
Mapper334::Mapper334(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), mirror_(header.mirror_mode) {
    reset();
}

Result<void> Mapper334::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 334U) {
        return std::unexpected("Mapper 334 requires mapper ID 334");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 334 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        !std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxChrSize) {
        return std::unexpected(
            "Mapper 334 requires power-of-two 32-128 KiB PRG and 8-256 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 334 uses registers, not RAM, in its MMC3 WRAM window");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 334 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

bool Mapper334::low_registers_accessible() const {
    return prg_ram_enabled_ && !prg_ram_write_protected_;
}

bool Mapper334::maps_prg(Address addr) const {
    if (addr >= 0x8000U) {
        return true;
    }
    return low_registers_accessible() && (addr & 0xE003U) == 0x6002U;
}

Byte Mapper334::prg_read_mask(Address addr) const {
    return maps_prg(addr) && addr < 0x8000U ? 0x01U : 0xFFU;
}

Byte Mapper334::read_prg(Address addr) {
    if (addr >= 0x8000U) {
        const auto offset = static_cast<std::size_t>(outer_bank_) * kPrgBankSize + (addr & 0x7FFFU);
        return prg_rom_[offset % prg_rom_.size()];
    }
    if (maps_prg(addr)) {
        return jumper_;
    }
    return 0U;
}

void Mapper334::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        if (!low_registers_accessible()) {
            return;
        }
        switch (addr & 0xE003U) {
        case 0x6000U:
            outer_bank_ = (value >> 1U) & 3U;
            break;
        case 0x6001U:
            unknown_register_ = value;
            break;
        default:
            break;
        }
        return;
    }

    switch (addr & 0xE001U) {
    case 0x8000U:
        bank_select_ = value & 0xC7U;
        break;
    case 0x8001U:
        bank_registers_[bank_select_ & 7U] = value;
        break;
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

Byte Mapper334::chr_bank(Address addr) const {
    const auto pattern_addr = static_cast<Address>(addr & 0x1FFFU);
    const unsigned slot = ((pattern_addr >> 10U) & 7U) ^ ((bank_select_ & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return static_cast<Byte>((bank_registers_[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return bank_registers_[slot - 2U];
}

Byte Mapper334::read_chr(Address addr) {
    const auto offset = static_cast<std::size_t>(chr_bank(addr)) * kChrBankSize +
                        (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper334::write_chr(Address, Byte) {}

MirrorMode Mapper334::mirror_mode() const {
    return mirror_;
}

Result<void> Mapper334::configure_board_jumpers(Byte value) {
    if (value > 1U) {
        return std::unexpected("Mapper 334 jumper must be 0 (20-in-1) or 1 (5-in-1)");
    }
    jumper_ = value;
    return {};
}

void Mapper334::reset() {
    outer_bank_ = 0U;
    unknown_register_ = 0U;
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

bool Mapper334::irq_pending() const {
    return irq_pending_;
}

void Mapper334::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper334::clock_irq_counter() {
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

Byte Mapper334::configuration_tag() const {
    return static_cast<Byte>(jumper_ | (initial_mirror_ == MirrorMode::Vertical ? 2U : 0U));
}

void Mapper334::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(configuration_tag());
    writer.write(outer_bank_);
    writer.write(unknown_register_);
    writer.write(bank_select_);
    writer.write_array(bank_registers_);
    writer.write<Byte>(mirror_ == MirrorMode::Vertical ? 1U : 0U);
    writer.write(irq_counter_);
    writer.write(irq_latch_);
    writer.write<Byte>((irq_reload_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write<Byte>((prg_ram_enabled_ ? 0x80U : 0U) | (prg_ram_write_protected_ ? 0x40U : 0U));
}

void Mapper334::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto configuration = reader.read<Byte>();
    const auto outer_bank = reader.read<Byte>();
    const auto unknown_register = reader.read<Byte>();
    const auto bank_select = reader.read<Byte>();
    std::array<Byte, 8> bank_registers{};
    reader.read_array(bank_registers);
    const auto mirror = reader.read<Byte>();
    const auto irq_counter = reader.read<Byte>();
    const auto irq_latch = reader.read<Byte>();
    const auto irq_flags = reader.read<Byte>();
    const auto ram_flags = reader.read<Byte>();

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || configuration != configuration_tag() || outer_bank > 3U ||
        (bank_select & 0x38U) != 0U || mirror > 1U || irq_flags > 7U ||
        ((irq_flags & 4U) != 0U && (irq_flags & 2U) == 0U) || (ram_flags & 0x3FU) != 0U) {
        reader.fail();
        return;
    }

    outer_bank_ = outer_bank;
    unknown_register_ = unknown_register;
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
}

} // namespace mapperbus::core
