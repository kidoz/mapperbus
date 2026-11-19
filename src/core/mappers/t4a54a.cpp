#include "core/mappers/t4a54a.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

T4a54a::T4a54a(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), mirror_mode_(initial_mirror_) {
    reset();
}

Result<void> T4a54a::validate_geometry(const INesHeader& header,
                                       std::size_t prg_size,
                                       std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 134 supports submapper 0");
    if (prg_size < 0x8000U || prg_size > 0x100000U || prg_size % 0x2000U != 0U ||
        chr_size < 0x2000U || chr_size > 0x100000U || chr_size % 0x400U != 0U)
        return std::unexpected("Mapper 134 requires 32 KiB-1 MiB PRG and 8 KiB-1 MiB CHR ROM");
    if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
        return std::unexpected("Mapper 134 supports ROM-only board memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 134 supports ordinary H/V CIRAM");
    return {};
}

Byte T4a54a::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    if ((mode_ & 0x20U) != 0U)
        return solder_pad_;
    return prg_rom_[prg_address(addr) % prg_rom_.size()];
}

void T4a54a::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        const auto reg = static_cast<Address>(addr & 0xE003U);
        const bool locked = (mode_ & 0x80U) != 0U;
        if (reg == 0x6002U) {
            cnrom_bank_ = locked ? static_cast<Byte>((cnrom_bank_ & 0xFCU) | (value & 3U)) : value;
        } else if (!locked) {
            if (reg == 0x6000U)
                mode_ = value & 0xF8U;
            else if (reg == 0x6001U)
                outer_bank_ = value;
        }
        return;
    }
    if (addr < 0x8000U)
        return;

    const bool even = (addr & 1U) == 0U;
    if (addr < 0xA000U) {
        if (even) {
            bank_select_ = value & 0xC7U;
        } else {
            auto& reg = bank_registers_[bank_select_ & 7U];
            reg = (bank_select_ & 7U) >= 6U ? value & 0x3FU : value;
        }
    } else if (addr < 0xC000U) {
        if (even)
            mirror_mode_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    } else if (addr < 0xE000U) {
        if (even)
            irq_reload_ = value;
        else {
            irq_reload_flag_ = true;
            irq_counter_ = 0U;
        }
    } else if (even) {
        irq_enabled_ = false;
        irq_pending_ = false;
    } else {
        irq_enabled_ = true;
    }
}

Byte T4a54a::read_chr(Address addr) {
    return chr_rom_[chr_address(addr) % chr_rom_.size()];
}

void T4a54a::write_chr(Address, Byte) {}

MirrorMode T4a54a::mirror_mode() const {
    return mirror_mode_;
}

Result<void> T4a54a::configure_board_jumpers(Byte value) {
    if (value > 1U)
        return std::unexpected("Mapper 134 solder pad accepts values 0 and 1");
    solder_pad_ = value;
    return {};
}

void T4a54a::reset() {
    mode_ = 0U;
    outer_bank_ = 0U;
    cnrom_bank_ = 0U;
    bank_select_ = 0U;
    bank_registers_.fill(0U);
    mirror_mode_ = initial_mirror_;
    irq_counter_ = 0U;
    irq_reload_ = 0U;
    irq_reload_flag_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
}

bool T4a54a::irq_pending() const {
    return irq_pending_;
}

void T4a54a::acknowledge_irq() {
    irq_pending_ = false;
}

void T4a54a::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_flag_) {
        irq_counter_ = irq_reload_;
        irq_reload_flag_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_)
        irq_pending_ = true;
}

std::size_t T4a54a::prg_address(Address addr) const {
    unsigned bank = 0U;
    std::size_t offset = 0U;
    if ((outer_bank_ & 0x80U) != 0U) {
        bank = bank_registers_[6];
        offset = bank * 0x2000U + (addr & 0x1FFFU);
        offset = (offset & ~0x2000U) | (addr & 0x2000U);
        if ((outer_bank_ & 8U) == 0U)
            offset = (offset & ~0x4000U) | (addr & 0x4000U);
    } else {
        const unsigned slot = (addr - 0x8000U) >> 13U;
        const bool prg_mode = (bank_select_ & 0x40U) != 0U;
        switch (slot) {
        case 0U:
            bank = prg_mode ? 62U : bank_registers_[6];
            break;
        case 1U:
            bank = bank_registers_[7];
            break;
        case 2U:
            bank = prg_mode ? bank_registers_[6] : 62U;
            break;
        default:
            bank = 63U;
            break;
        }
        offset = bank * 0x2000U + (addr & 0x1FFFU);
    }
    offset = (offset & 0x7FFFFU) | (static_cast<std::size_t>(mode_ & 0x10U) << 15U);
    offset = (offset & ~0x40000U) | (static_cast<std::size_t>(outer_bank_ & 2U) << 17U);
    if ((outer_bank_ & 4U) != 0U)
        offset = (offset & ~0x20000U) | (static_cast<std::size_t>(outer_bank_ & 1U) << 17U);
    return offset;
}

unsigned T4a54a::mmc3_chr_bank(Address addr) const {
    const unsigned slot = ((addr >> 10U) & 7U) ^ ((bank_select_ & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U)
        return (bank_registers_[slot / 2U] & 0xFEU) | (slot & 1U);
    return bank_registers_[slot - 2U];
}

std::size_t T4a54a::chr_address(Address addr) const {
    std::size_t offset = 0U;
    if ((mode_ & 8U) != 0U) {
        const auto mask = (mode_ & 0x40U) != 0U ? 0x0FU : 0x1FU;
        offset = static_cast<std::size_t>(cnrom_bank_ & mask) * 0x2000U + (addr & 0x1FFFU);
    } else {
        offset = mmc3_chr_bank(addr) * 0x400U + (addr & 0x3FFU);
    }
    offset = (offset & 0x7FFFFU) | (static_cast<std::size_t>(mode_ & 0x40U) << 13U);
    offset = (offset & ~0x40000U) | (static_cast<std::size_t>(outer_bank_ & 0x20U) << 13U);
    if ((outer_bank_ & 0x40U) != 0U)
        offset = (offset & ~0x20000U) | (static_cast<std::size_t>(outer_bank_ & 0x10U) << 13U);
    return offset;
}

uint16_t T4a54a::state_tag() const {
    return static_cast<uint16_t>(0x8600U | (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U) |
                                 (solder_pad_ != 0U ? 2U : 0U));
}

void T4a54a::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(mode_);
    writer.write(outer_bank_);
    writer.write(cnrom_bank_);
    writer.write(bank_select_);
    writer.write_array(bank_registers_);
    writer.write<Byte>(static_cast<Byte>(mirror_mode_));
    writer.write(irq_counter_);
    writer.write(irq_reload_);
    writer.write<Byte>((irq_reload_flag_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
}

void T4a54a::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto mode = reader.read<Byte>();
    const auto outer = reader.read<Byte>();
    const auto cnrom = reader.read<Byte>();
    const auto select = reader.read<Byte>();
    std::array<Byte, 8> banks{};
    reader.read_array(banks);
    const auto mirror = reader.read<Byte>();
    const auto counter = reader.read<Byte>();
    const auto reload = reader.read<Byte>();
    const auto irq = reader.read<Byte>();
    if (!reader.ok() || reader.remaining() != 0U || tag != state_tag() || (mode & 7U) != 0U ||
        (select & 0x38U) != 0U || (banks[6] & 0xC0U) != 0U || (banks[7] & 0xC0U) != 0U ||
        mirror > 1U || (irq & 0xF8U) != 0U || ((irq & 4U) != 0U && (irq & 2U) == 0U)) {
        reader.fail();
        return;
    }
    mode_ = mode;
    outer_bank_ = outer;
    cnrom_bank_ = cnrom;
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
