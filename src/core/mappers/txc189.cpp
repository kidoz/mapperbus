#include "core/mappers/txc189.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

// NESdev mapper 189 r22901 and its linked PCB reports. The generalized latch
// intentionally accepts every board variant without filename or hash detection.
Txc189::Txc189(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), mirror_mode_(header.mirror_mode) {
    reset();
}

Result<void> Txc189::validate_geometry(const INesHeader& header,
                                       std::size_t prg_size,
                                       std::size_t chr_size) {
    if (header.mapper_number != 189U || (header.is_nes2 && header.submapper != 0U))
        return std::unexpected("Mapper 189 supports only mapper 189 submapper 0");
    if (prg_size != 0x20000U && prg_size != 0x40000U)
        return std::unexpected("Mapper 189 supports the documented 128/256 KiB PRG ROMs");
    if (chr_size < 0x2000U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
        return std::unexpected("Mapper 189 requires 8-256 KiB CHR ROM in 1 KiB units");
    if (header.has_battery || header.prg_nvram_size != 0U || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U || (header.is_nes2 && header.prg_ram_size != 0U))
        return std::unexpected("Mapper 189 supports ROM-only cartridge memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 189 supports ordinary H/V CIRAM");
    return {};
}

Byte Txc189::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty())
        return 0U;
    const auto offset = static_cast<std::size_t>(prg_bank_) * 0x8000U + (addr & 0x7FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Txc189::write_prg(Address addr, Byte value) {
    if (addr >= 0x4020U && addr < 0x8000U) {
        prg_bank_ = static_cast<Byte>((value | (value >> 4U)) & 0x0FU);
        return;
    }
    if (addr < 0x8000U)
        return;

    switch (addr & 0xE001U) {
    case 0x8000U:
        bank_select_ = value & 7U;
        prg_mode_ = (value & 0x40U) != 0U;
        chr_inversion_ = (value & 0x80U) != 0U;
        update_chr_banks();
        break;
    case 0x8001U:
        bank_registers_[bank_select_] = bank_select_ >= 6U ? value & 0x3FU : value;
        update_chr_banks();
        break;
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

bool Txc189::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Txc189::prg_read_mask(Address addr) const {
    return addr >= 0x8000U ? 0xFFU : 0U;
}

void Txc189::write_expansion(Address addr, Byte value) {
    write_prg(addr, value);
}

void Txc189::update_chr_banks() {
    if (chr_rom_.empty()) {
        chr_offsets_.fill(0U);
        return;
    }
    const auto bank_count = chr_rom_.size() / 0x400U;
    const auto bank = [bank_count](Byte value) {
        return static_cast<uint32_t>((static_cast<std::size_t>(value) % bank_count) * 0x400U);
    };
    if (!chr_inversion_) {
        chr_offsets_[0] = bank(bank_registers_[0] & 0xFEU);
        chr_offsets_[1] = bank(bank_registers_[0] | 1U);
        chr_offsets_[2] = bank(bank_registers_[1] & 0xFEU);
        chr_offsets_[3] = bank(bank_registers_[1] | 1U);
        chr_offsets_[4] = bank(bank_registers_[2]);
        chr_offsets_[5] = bank(bank_registers_[3]);
        chr_offsets_[6] = bank(bank_registers_[4]);
        chr_offsets_[7] = bank(bank_registers_[5]);
    } else {
        chr_offsets_[0] = bank(bank_registers_[2]);
        chr_offsets_[1] = bank(bank_registers_[3]);
        chr_offsets_[2] = bank(bank_registers_[4]);
        chr_offsets_[3] = bank(bank_registers_[5]);
        chr_offsets_[4] = bank(bank_registers_[0] & 0xFEU);
        chr_offsets_[5] = bank(bank_registers_[0] | 1U);
        chr_offsets_[6] = bank(bank_registers_[1] & 0xFEU);
        chr_offsets_[7] = bank(bank_registers_[1] | 1U);
    }
}

Byte Txc189::read_chr(Address addr) {
    if (chr_rom_.empty())
        return 0U;
    const auto offset = chr_offsets_[(addr >> 10U) & 7U] + (addr & 0x3FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Txc189::write_chr(Address, Byte) {}

MirrorMode Txc189::mirror_mode() const {
    return mirror_mode_;
}

void Txc189::reset() {
    prg_bank_ = 0U;
    bank_select_ = 0U;
    prg_mode_ = false;
    chr_inversion_ = false;
    bank_registers_.fill(0U);
    mirror_mode_ = initial_mirror_;
    irq_counter_ = 0U;
    irq_reload_ = 0U;
    irq_reload_flag_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
    update_chr_banks();
}

bool Txc189::irq_pending() const {
    return irq_pending_;
}

void Txc189::acknowledge_irq() {
    irq_pending_ = false;
}

void Txc189::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_flag_) {
        irq_counter_ = irq_reload_;
        irq_reload_flag_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_)
        irq_pending_ = true;
}

uint16_t Txc189::state_tag() const {
    return static_cast<uint16_t>(0xBD00U | (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Txc189::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<uint32_t>(prg_rom_.size()));
    writer.write(static_cast<uint32_t>(chr_rom_.size()));
    writer.write(prg_bank_);
    writer.write(bank_select_);
    writer.write<Byte>((prg_mode_ ? 0x40U : 0U) | (chr_inversion_ ? 0x80U : 0U));
    writer.write_array(bank_registers_);
    writer.write<Byte>(static_cast<Byte>(mirror_mode_));
    writer.write(irq_counter_);
    writer.write(irq_reload_);
    writer.write<Byte>((irq_reload_flag_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
}

void Txc189::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_size = reader.read<uint32_t>();
    const auto chr_size = reader.read<uint32_t>();
    const auto prg_bank = reader.read<Byte>();
    const auto bank_select = reader.read<Byte>();
    const auto mode = reader.read<Byte>();
    std::array<Byte, 8> banks{};
    reader.read_array(banks);
    const auto mirror = reader.read<Byte>();
    const auto counter = reader.read<Byte>();
    const auto reload = reader.read<Byte>();
    const auto irq = reader.read<Byte>();

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || prg_bank > 0x0FU || bank_select > 7U ||
        (mode & 0x3FU) != 0U || banks[6] > 0x3FU || banks[7] > 0x3FU ||
        mirror > static_cast<Byte>(MirrorMode::Vertical) || irq > 7U ||
        ((irq & 4U) != 0U && (irq & 2U) == 0U)) {
        reader.fail();
        return;
    }

    prg_bank_ = prg_bank;
    bank_select_ = bank_select;
    prg_mode_ = (mode & 0x40U) != 0U;
    chr_inversion_ = (mode & 0x80U) != 0U;
    bank_registers_ = banks;
    mirror_mode_ = static_cast<MirrorMode>(mirror);
    irq_counter_ = counter;
    irq_reload_ = reload;
    irq_reload_flag_ = (irq & 1U) != 0U;
    irq_enabled_ = (irq & 2U) != 0U;
    irq_pending_ = (irq & 4U) != 0U;
    update_chr_banks();
}

} // namespace mapperbus::core
