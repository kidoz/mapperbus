#include "core/mappers/mapper267.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

Mapper267::Mapper267(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), mirror_mode_(initial_mirror_) {
    reset();
}

Result<void> Mapper267::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 267 supports submapper 0");
    if (prg_size < 0x8000U || prg_size > 0x200000U || prg_size % 0x2000U != 0U ||
        chr_size < 0x2000U || chr_size > 0x100000U || chr_size % 0x400U != 0U)
        return std::unexpected("Mapper 267 requires 32 KiB-2 MiB PRG and 8 KiB-1 MiB CHR ROM");
    if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
        return std::unexpected("Mapper 267 supports ROM-only board memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 267 supports ordinary H/V CIRAM");
    return {};
}

Byte Mapper267::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    const auto offset = static_cast<std::size_t>(prg_bank(addr)) * 0x2000U + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper267::write_prg(Address addr, Byte value) {
    if (addr == 0x6000U) {
        outer_bank_ = value;
        return;
    }
    if (addr < 0x8000U)
        return;

    switch (addr & 0xE001U) {
    case 0x8000U:
        bank_select_ = value & 0xC7U;
        break;
    case 0x8001U: {
        const unsigned index = bank_select_ & 7U;
        bank_registers_[index] = value & (index >= 6U ? 0x1FU : 0x7FU);
        break;
    }
    case 0xA000U:
        mirror_mode_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
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

Byte Mapper267::read_chr(Address addr) {
    const auto offset = static_cast<std::size_t>(chr_bank(addr)) * 0x400U + (addr & 0x3FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper267::write_chr(Address, Byte) {}

MirrorMode Mapper267::mirror_mode() const {
    return mirror_mode_;
}

void Mapper267::reset() {
    outer_bank_ = 0U;
    bank_select_ = 0U;
    bank_registers_.fill(0U);
    mirror_mode_ = initial_mirror_;
    irq_counter_ = 0U;
    irq_reload_ = 0U;
    irq_reload_flag_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
}

bool Mapper267::irq_pending() const {
    return irq_pending_;
}

void Mapper267::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper267::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_flag_) {
        irq_counter_ = irq_reload_;
        irq_reload_flag_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_)
        irq_pending_ = true;
}

unsigned Mapper267::outer_bank() const {
    return ((outer_bank_ & 0x20U) >> 3U) | ((outer_bank_ & 0x06U) >> 1U);
}

unsigned Mapper267::prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) >> 13U;
    const bool swapped = (bank_select_ & 0x40U) != 0U;
    unsigned inner = 0U;
    switch (slot) {
    case 0U:
        inner = swapped ? 30U : bank_registers_[6];
        break;
    case 1U:
        inner = bank_registers_[7];
        break;
    case 2U:
        inner = swapped ? bank_registers_[6] : 30U;
        break;
    default:
        inner = 31U;
        break;
    }
    return inner | (outer_bank() << 5U);
}

unsigned Mapper267::chr_bank(Address addr) const {
    const unsigned slot = ((addr >> 10U) & 7U) ^ ((bank_select_ & 0x80U) != 0U ? 4U : 0U);
    unsigned inner = 0U;
    if (slot < 4U)
        inner = (bank_registers_[slot / 2U] & 0x7EU) | (slot & 1U);
    else
        inner = bank_registers_[slot - 2U] & 0x7FU;
    return inner | (outer_bank() << 7U);
}

uint16_t Mapper267::state_tag() const {
    return static_cast<uint16_t>(0xA700U | (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper267::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(outer_bank_);
    writer.write(bank_select_);
    writer.write_array(bank_registers_);
    writer.write<Byte>(static_cast<Byte>(mirror_mode_));
    writer.write(irq_counter_);
    writer.write(irq_reload_);
    writer.write<Byte>((irq_reload_flag_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
}

void Mapper267::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto outer = reader.read<Byte>();
    const auto select = reader.read<Byte>();
    std::array<Byte, 8> banks{};
    reader.read_array(banks);
    const auto mirror = reader.read<Byte>();
    const auto counter = reader.read<Byte>();
    const auto reload = reader.read<Byte>();
    const auto irq = reader.read<Byte>();
    if (!reader.ok() || reader.remaining() != 0U || tag != state_tag() || (select & 0x38U) != 0U ||
        (banks[6] & 0xE0U) != 0U || (banks[7] & 0xE0U) != 0U ||
        std::any_of(
            banks.begin(), banks.begin() + 6, [](Byte bank) { return (bank & 0x80U) != 0U; }) ||
        mirror > 1U || (irq & 0xF8U) != 0U || ((irq & 4U) != 0U && (irq & 2U) == 0U)) {
        reader.fail();
        return;
    }
    outer_bank_ = outer;
    bank_select_ = select;
    bank_registers_ = banks;
    mirror_mode_ = static_cast<MirrorMode>(mirror);
    irq_counter_ = counter;
    irq_reload_ = reload;
    irq_reload_flag_ = (irq & 1U) != 0U;
    irq_enabled_ = (irq & 2U) != 0U;
    irq_pending_ = (irq & 4U) != 0U;
}

} // namespace mapperbus::core
