#include "core/mappers/mapper287.hpp"

#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t kOuterBankSize = 128U * 1024U;
constexpr std::size_t kMaxRomSize = 4U * kOuterBankSize;
} // namespace

// Original implementation from NESdev mapper 287 r7732 and its MMC3 register
// reference. Exact electrical reset defaults and the clone's IRQ revision are
// unspecified; reset and IRQ behavior follow the core's deterministic MMC3 policy.
Mapper287::Mapper287(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), mirror_(header.mirror_mode) {
    reset();
}

Result<void> Mapper287::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (!header.is_nes2 || header.submapper != 0U)
        return std::unexpected("Mapper 287 requires NES 2.0 submapper 0");
    if (prg_size < 32768U || prg_size > kMaxRomSize || prg_size % 8192U != 0U)
        return std::unexpected("Mapper 287 requires 32-512 KiB PRG ROM in 8 KiB units");
    if (chr_size < 8192U || chr_size > kMaxRomSize || chr_size % 1024U != 0U)
        return std::unexpected("Mapper 287 requires 8-512 KiB CHR ROM in 1 KiB units");
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U)
        return std::unexpected("Mapper 287 supports ROM-only board memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 287 requires H/V nametable wiring");
    return {};
}

bool Mapper287::nrom_mode() const {
    return (outer_bank_ & 0x08U) != 0U || (dip_switch_ != 0U && (outer_bank_ & 0x04U) != 0U);
}

Byte Mapper287::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;

    const std::size_t outer = static_cast<std::size_t>(outer_bank_ & 3U) * kOuterBankSize;
    std::size_t offset = 0U;
    if (nrom_mode()) {
        const std::size_t inner = static_cast<std::size_t>((outer_bank_ >> 4U) & 3U) * 32768U;
        offset = outer + inner + (addr & 0x7FFFU);
    } else {
        const unsigned slot = (addr - 0x8000U) >> 13U;
        unsigned bank = 0U;
        if (slot == 1U)
            bank = bank_registers_[7U] & 0x0FU;
        else if (slot == 3U)
            bank = 15U;
        else if ((slot == 2U) != ((bank_select_ & 0x40U) != 0U))
            bank = 14U;
        else
            bank = bank_registers_[6U] & 0x0FU;
        offset = outer + bank * 8192U + (addr & 0x1FFFU);
    }
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper287::write_prg(Address addr, Byte value) {
    if ((addr & 0xE000U) == 0x6000U) {
        if (ram_enabled_ && !ram_write_protected_)
            outer_bank_ = value & 0x3FU;
        return;
    }
    if (addr < 0x8000U)
        return;

    switch (addr & 0xE001U) {
    case 0x8000U:
        bank_select_ = value & 0xC7U;
        break;
    case 0x8001U:
        bank_registers_[bank_select_ & 7U] =
            (bank_select_ & 7U) >= 6U ? value & 0x0FU : value & 0x7FU;
        break;
    case 0xA000U:
        mirror_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
        ram_enabled_ = (value & 0x80U) != 0U;
        ram_write_protected_ = (value & 0x40U) != 0U;
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

bool Mapper287::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper287::read_chr(Address addr) {
    const unsigned slot = ((addr >> 10U) & 7U) ^ ((bank_select_ & 0x80U) != 0U ? 4U : 0U);
    unsigned bank = 0U;
    if (slot < 4U)
        bank = (bank_registers_[slot >> 1U] & 0xFEU) | (slot & 1U);
    else
        bank = bank_registers_[slot - 2U];

    const std::size_t outer = static_cast<std::size_t>(outer_bank_ & 3U) * kOuterBankSize;
    const std::size_t offset = outer + (bank & 0x7FU) * 1024U + (addr & 0x3FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper287::write_chr(Address, Byte) {}

MirrorMode Mapper287::mirror_mode() const {
    return mirror_;
}

void Mapper287::power_on() {
    reset();
}

void Mapper287::reset() {
    outer_bank_ = 0U;
    bank_select_ = 0U;
    bank_registers_.fill(0U);
    mirror_ = initial_mirror_;
    irq_counter_ = 0U;
    irq_latch_ = 0U;
    irq_reload_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
    ram_enabled_ = true;
    ram_write_protected_ = false;
}

Result<void> Mapper287::configure_board_jumpers(Byte value) {
    if (value > 1U)
        return std::unexpected("Mapper 287 DIP switch accepts values 0 and 1");
    dip_switch_ = value;
    return {};
}

bool Mapper287::irq_pending() const {
    return irq_pending_;
}

void Mapper287::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper287::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_) {
        irq_counter_ = irq_latch_;
        irq_reload_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_)
        irq_pending_ = true;
}

uint16_t Mapper287::state_tag() const {
    return static_cast<uint16_t>(0x11F0U | dip_switch_ |
                                 (initial_mirror_ == MirrorMode::Vertical ? 2U : 0U));
}

void Mapper287::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(outer_bank_);
    writer.write(bank_select_);
    writer.write_array(bank_registers_);
    writer.write(static_cast<Byte>(mirror_));
    writer.write(irq_counter_);
    writer.write(irq_latch_);
    writer.write<Byte>((irq_reload_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write<Byte>((ram_enabled_ ? 0x80U : 0U) | (ram_write_protected_ ? 0x40U : 0U));
}

void Mapper287::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto outer = reader.read<Byte>();
    const auto select = reader.read<Byte>();
    std::array<Byte, 8> banks{};
    reader.read_array(banks);
    const auto mirror = reader.read<Byte>();
    const auto counter = reader.read<Byte>();
    const auto latch = reader.read<Byte>();
    const auto irq = reader.read<Byte>();
    const auto ram = reader.read<Byte>();

    const bool valid_banks = [&banks] {
        for (unsigned index = 0U; index < banks.size(); ++index)
            if (banks[index] > (index < 6U ? 0x7FU : 0x0FU))
                return false;
        return true;
    }();
    const bool valid_irq = irq <= 7U && ((irq & 4U) == 0U || (irq & 2U) != 0U);
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || outer > 0x3FU || (select & 0x38U) != 0U || !valid_banks ||
        mirror > static_cast<Byte>(MirrorMode::Vertical) || !valid_irq || (ram & 0x3FU) != 0U) {
        reader.fail();
        return;
    }

    outer_bank_ = outer;
    bank_select_ = select;
    bank_registers_ = banks;
    mirror_ = static_cast<MirrorMode>(mirror);
    irq_counter_ = counter;
    irq_latch_ = latch;
    irq_reload_ = (irq & 1U) != 0U;
    irq_enabled_ = (irq & 2U) != 0U;
    irq_pending_ = (irq & 4U) != 0U;
    ram_enabled_ = (ram & 0x80U) != 0U;
    ram_write_protected_ = (ram & 0x40U) != 0U;
}

} // namespace mapperbus::core
