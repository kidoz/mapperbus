#include "core/mappers/mapper091.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

// Original implementation from NESdev iNES Mapper 091, revision 22474.
// Electrical reset values are unspecified; reset deterministically clears all
// registers and restores the header's hard-wired/initial H/V arrangement.
Mapper091::Mapper091(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : submapper_(header.is_nes2 ? header.submapper : 0U), initial_mirror_(header.mirror_mode),
      prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> Mapper091::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 91U || (!header.is_nes2 && header.submapper != 0U) ||
        (header.is_nes2 && header.submapper > 1U))
        return std::unexpected("Mapper 091 supports legacy submapper 0 and NES 2.0 submappers 0-1");

    const Byte submapper = header.is_nes2 ? header.submapper : 0U;
    const std::size_t max_prg = submapper == 0U ? 524288U : 2097152U;
    const std::size_t max_chr = submapper == 0U ? 1048576U : 524288U;
    if (prg_size < 32768U || prg_size > max_prg || !std::has_single_bit(prg_size) ||
        chr_size < 8192U || chr_size > max_chr || !std::has_single_bit(chr_size))
        return std::unexpected("Unsupported Mapper 091 PRG/CHR ROM geometry");
    if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
        return std::unexpected("Mapper 091 requires ROM-only PRG/CHR memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 091 supports ordinary H/V nametable mirroring");
    return {};
}

std::size_t Mapper091::prg_offset(Address addr) const {
    const auto offset = static_cast<std::size_t>(addr & 0x1FFFU);
    if (submapper_ == 0U) {
        const std::size_t outer = static_cast<std::size_t>((outer_bank_ >> 1U) & 3U) << 17U;
        const unsigned slot = (addr - 0x8000U) >> 13U;
        const unsigned bank = slot < 2U ? prg_banks_[slot] & 0x0FU : 12U + slot;
        return (outer + static_cast<std::size_t>(bank) * 8192U + offset) % prg_rom_.size();
    }
    if (addr >= 0xC000U)
        return prg_rom_.size() - 16384U + (addr & 0x3FFFU);
    const unsigned slot = (addr - 0x8000U) >> 13U;
    return (static_cast<std::size_t>(prg_banks_[slot]) * 8192U + offset) % prg_rom_.size();
}

Byte Mapper091::read_prg(Address addr) {
    return addr >= 0x8000U ? prg_rom_[prg_offset(addr)] : 0U;
}

void Mapper091::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U) {
        if (submapper_ == 0U && addr < 0xA000U)
            outer_bank_ = value & 7U;
        return;
    }
    if (addr < 0x6000U)
        return;

    const Address mask = submapper_ == 0U ? 0xF003U : 0xF007U;
    const Address reg = addr & mask;
    if (reg >= 0x6000U && reg <= 0x6003U) {
        chr_banks_[reg & 3U] = value;
        return;
    }
    if (submapper_ == 1U) {
        if (reg == 0x6004U || reg == 0x6005U) {
            mirror_ = reg == 0x6004U ? MirrorMode::Horizontal : MirrorMode::Vertical;
            return;
        }
        if (reg == 0x6006U) {
            irq_counter_ = static_cast<uint16_t>((irq_counter_ & 0xFF00U) | value);
            return;
        }
        if (reg == 0x6007U) {
            irq_counter_ = static_cast<uint16_t>((irq_counter_ & 0x00FFU) |
                                                 (static_cast<unsigned>(value) << 8U));
            return;
        }
    }
    if (reg == 0x7000U || reg == 0x7001U) {
        prg_banks_[reg & 1U] = value;
        return;
    }
    if (reg == (0x7006U & mask)) {
        irq_enabled_ = false;
        irq_pending_ = false;
        return;
    }
    if (reg == (0x7007U & mask)) {
        irq_enabled_ = true;
        irq_pending_ = false;
        irq_phase_ = 0U;
        if (submapper_ == 0U)
            irq_position_ = 0U;
    }
}

std::size_t Mapper091::chr_offset(Address addr) const {
    const unsigned slot = (addr & 0x1FFFU) >> 11U;
    const std::size_t outer =
        submapper_ == 0U ? static_cast<std::size_t>(outer_bank_ & 1U) << 19U : 0U;
    return (outer + static_cast<std::size_t>(chr_banks_[slot]) * 2048U + (addr & 0x7FFU)) %
           chr_rom_.size();
}

Byte Mapper091::read_chr(Address addr) {
    return chr_rom_[chr_offset(addr)];
}

void Mapper091::write_chr(Address, Byte) {}

MirrorMode Mapper091::mirror_mode() const {
    return mirror_;
}

void Mapper091::reset() {
    prg_banks_.fill(0U);
    chr_banks_.fill(0U);
    outer_bank_ = 0U;
    mirror_ = initial_mirror_;
    irq_counter_ = 0U;
    irq_phase_ = 0U;
    irq_position_ = 0U;
    irq_enabled_ = false;
    irq_pending_ = false;
    ppu_a12_ = false;
}

bool Mapper091::irq_pending() const {
    return irq_pending_;
}

void Mapper091::clock_cpu() {
    if (submapper_ != 1U || !irq_enabled_)
        return;
    if (irq_phase_ == 3U) {
        irq_pending_ = irq_pending_ || irq_counter_ < 5U;
        irq_counter_ = static_cast<uint16_t>(irq_counter_ - 5U);
    }
    irq_phase_ = (irq_phase_ + 1U) & 3U;
}

bool Mapper091::observes_ppu_a12() const {
    return submapper_ == 0U;
}

void Mapper091::observe_ppu_a12(bool high) {
    if (submapper_ != 0U)
        return;
    if (high && !ppu_a12_ && irq_enabled_) {
        ++irq_position_;
        if (irq_position_ == 64U) {
            irq_position_ = 0U;
            irq_pending_ = true;
        }
    }
    ppu_a12_ = high;
}

uint16_t Mapper091::state_tag() const {
    return static_cast<uint16_t>(0x5B00U | submapper_ |
                                 (initial_mirror_ == MirrorMode::Vertical ? 0x10U : 0U));
}

void Mapper091::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write<uint32_t>(static_cast<uint32_t>(prg_rom_.size()));
    writer.write<uint32_t>(static_cast<uint32_t>(chr_rom_.size()));
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write(outer_bank_);
    writer.write(irq_counter_);
    writer.write(irq_phase_);
    writer.write(irq_position_);
    const Byte flags = static_cast<Byte>((irq_enabled_ ? 1U : 0U) | (irq_pending_ ? 2U : 0U) |
                                         (ppu_a12_ ? 4U : 0U));
    writer.write(flags);
    writer.write<Byte>(mirror_ == MirrorMode::Vertical ? 1U : 0U);
}

void Mapper091::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_size = reader.read<uint32_t>();
    const auto chr_size = reader.read<uint32_t>();
    std::array<Byte, 2> prg_banks{};
    std::array<Byte, 4> chr_banks{};
    reader.read_array(prg_banks);
    reader.read_array(chr_banks);
    const auto outer_bank = reader.read<Byte>();
    const auto irq_counter = reader.read<uint16_t>();
    const auto irq_phase = reader.read<Byte>();
    const auto irq_position = reader.read<Byte>();
    const auto flags = reader.read<Byte>();
    const auto mirror = reader.read<Byte>();

    const bool valid_submapper_fields =
        submapper_ == 0U
            ? outer_bank <= 7U && irq_counter == 0U && irq_phase == 0U && irq_position < 64U &&
                  mirror == (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U)
            : outer_bank == 0U && irq_phase < 4U && irq_position == 0U && (flags & 4U) == 0U &&
                  mirror <= 1U;
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || flags > 7U || !valid_submapper_fields) {
        reader.fail();
        return;
    }

    prg_banks_ = prg_banks;
    chr_banks_ = chr_banks;
    outer_bank_ = outer_bank;
    irq_counter_ = irq_counter;
    irq_phase_ = irq_phase;
    irq_position_ = irq_position;
    irq_enabled_ = (flags & 1U) != 0U;
    irq_pending_ = (flags & 2U) != 0U;
    ppu_a12_ = (flags & 4U) != 0U;
    mirror_ = mirror == 1U ? MirrorMode::Vertical : MirrorMode::Horizontal;
}

} // namespace mapperbus::core
