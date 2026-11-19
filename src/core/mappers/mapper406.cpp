#include "core/mappers/mapper406.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x040000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kFlashSectorSize = 0x010000UZ;
constexpr std::uint16_t kStateTag = 0x4060U;
constexpr std::uint32_t kProgramCycles = 32U;
constexpr std::uint32_t kSectorCycles = 36000U;
constexpr std::uint32_t kChipCycles = 144000U;

} // namespace

// Original implementation from NESdev mapper 406 oldid 22429. Flash command timing
// follows the existing deterministic flash-board policy; the 29F040 has 64 KiB sectors.
// Console reset restores the emulated MMC3 defaults but has no flash reset connection.
Mapper406::Mapper406(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : flash_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), submapper_(header.submapper),
      initial_mirror_(header.mirror_mode) {
    const auto source_size = flash_.size();
    flash_.resize(kPrgSize, 0xFFU);
    if (source_size < kPrgSize && source_size != 0U) {
        for (std::size_t offset = source_size; offset < kPrgSize; ++offset) {
            flash_[offset] = flash_[offset % source_size];
        }
    }
    power_on();
}

Result<void> Mapper406::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 406U) {
        return std::unexpected("Mapper 406 requires mapper ID 406");
    }
    if (!header.is_nes2 || header.submapper > 1U) {
        return std::unexpected("Mapper 406 requires NES 2.0 submapper 0 or 1");
    }
    if (prg_size != kPrgSize || chr_size < kMinChrSize || chr_size > kMaxChrSize ||
        chr_size % kChrBankSize != 0U) {
        return std::unexpected(
            "Mapper 406 requires 512 KiB PRG flash and 8-256 KiB CHR ROM in 1 KiB units");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 406 provides flash storage but no CPU or CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 406 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 406 does not provide trainer RAM");
    }
    return {};
}

std::size_t Mapper406::prg_address(Address addr) const {
    const auto slot = static_cast<unsigned>((addr - 0x8000U) / kPrgBankSize);
    const bool swapped = (registers_.bank_select & 0x40U) != 0U;
    unsigned bank = 63U;
    if (slot == 1U) {
        bank = registers_.banks[7U];
    } else if (slot == 2U) {
        bank = swapped ? registers_.banks[6U] : 62U;
    } else if (slot == 0U) {
        bank = swapped ? 62U : registers_.banks[6U];
    }
    return static_cast<std::size_t>(bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

Byte Mapper406::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    const auto address = prg_address(addr);
    if (flash_cycles_ != 0U) {
        const auto value = static_cast<Byte>((flash_operation_ == FlashOperation::Program
                                                  ? (~static_cast<unsigned>(pending_data_) & 0x80U)
                                                  : 0U) |
                                             (flash_toggle_ == 0U ? 0x40U : 0U));
        flash_toggle_ ^= 1U;
        return value;
    }
    if (flash_id_mode_) {
        if (address == 0U) {
            return submapper_ == 0U ? 0xC2U : 0x01U;
        }
        if (address == 1U) {
            return 0xA4U;
        }
        return 0xFFU;
    }
    return flash_[address];
}

void Mapper406::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }

    write_flash(prg_address(addr), value);

    Address decoded = addr;
    if (submapper_ == 0U) {
        decoded = static_cast<Address>((addr & 0xE000U) | ((addr >> 1U) & 1U));
    } else if ((addr & 0xE000U) == 0x8000U || (addr & 0xE000U) == 0xE000U) {
        decoded = static_cast<Address>(addr ^ 0x6000U);
    }
    write_mmc3(static_cast<Address>(decoded & 0xE001U), value);
}

void Mapper406::write_mmc3(Address addr, Byte value) {
    switch (addr) {
    case 0x8000U:
        registers_.bank_select = value & 0xC7U;
        break;
    case 0x8001U: {
        const auto index = registers_.bank_select & 7U;
        registers_.banks[index] = index >= 6U ? value & 0x3FU : value;
        break;
    }
    case 0xA000U:
        registers_.mirror = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
        break;
    case 0xC000U:
        registers_.irq_latch = value;
        break;
    case 0xC001U:
        registers_.irq_reload = true;
        registers_.irq_counter = 0U;
        break;
    case 0xE000U:
        registers_.irq_enabled = false;
        registers_.irq_pending = false;
        break;
    case 0xE001U:
        registers_.irq_enabled = true;
        break;
    default:
        break;
    }
}

bool Mapper406::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper406::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

std::size_t Mapper406::chr_address(Address addr) const {
    auto slot = static_cast<unsigned>((addr >> 10U) & 7U);
    if ((registers_.bank_select & 0x80U) != 0U) {
        slot ^= 4U;
    }
    const auto bank =
        slot < 4U ? static_cast<unsigned>((registers_.banks[slot / 2U] & 0xFEU) | (slot & 1U))
                  : static_cast<unsigned>(registers_.banks[slot - 2U]);
    const auto bank_count = chr_rom_.size() / kChrBankSize;
    return (static_cast<std::size_t>(bank_count == 0U ? 0U : bank % bank_count) * kChrBankSize) +
           (addr & (kChrBankSize - 1U));
}

Byte Mapper406::read_chr(Address addr) {
    return chr_rom_.empty() ? 0U : chr_rom_[chr_address(addr)];
}

void Mapper406::write_chr(Address, Byte) {}

MirrorMode Mapper406::mirror_mode() const {
    return registers_.mirror;
}

void Mapper406::power_on() {
    reset();
}

void Mapper406::reset() {
    registers_ = Registers{};
    registers_.mirror = initial_mirror_;
}

bool Mapper406::irq_pending() const {
    return registers_.irq_pending;
}

void Mapper406::clock_irq_counter() {
    if (registers_.irq_counter == 0U || registers_.irq_reload) {
        registers_.irq_counter = registers_.irq_latch;
        registers_.irq_reload = false;
    } else {
        --registers_.irq_counter;
    }
    if (registers_.irq_counter == 0U && registers_.irq_enabled) {
        registers_.irq_pending = true;
    }
}

void Mapper406::begin_flash(FlashOperation operation,
                            std::size_t address,
                            Byte data,
                            std::uint32_t cycles) {
    flash_operation_ = operation;
    pending_address_ = static_cast<std::uint32_t>(address);
    pending_data_ = data;
    flash_cycles_ = cycles;
    flash_toggle_ = 0U;
    flash_id_mode_ = false;
    flash_command_ = FlashCommand::Read;
}

void Mapper406::write_flash(std::size_t address, Byte value) {
    if (flash_cycles_ != 0U) {
        return;
    }
    address &= kPrgSize - 1U;
    const auto decoded = address & 0x7FFFU;
    if (flash_command_ == FlashCommand::Program) {
        begin_flash(FlashOperation::Program, address, value, kProgramCycles);
        return;
    }
    if (value == 0xF0U) {
        flash_command_ = FlashCommand::Read;
        flash_id_mode_ = false;
        return;
    }

    switch (flash_command_) {
    case FlashCommand::Read:
        if (decoded == 0x5555U && value == 0xAAU) {
            flash_command_ = FlashCommand::Unlock1;
        }
        break;
    case FlashCommand::Unlock1:
        flash_command_ =
            decoded == 0x2AAAU && value == 0x55U ? FlashCommand::Unlock2 : FlashCommand::Read;
        break;
    case FlashCommand::Unlock2:
        flash_command_ = FlashCommand::Read;
        if (decoded == 0x5555U) {
            if (value == 0xA0U) {
                flash_command_ = FlashCommand::Program;
            } else if (value == 0x80U) {
                flash_command_ = FlashCommand::Erase1;
            } else if (value == 0x90U) {
                flash_id_mode_ = true;
            }
        }
        break;
    case FlashCommand::Erase1:
        flash_command_ =
            decoded == 0x5555U && value == 0xAAU ? FlashCommand::Erase2 : FlashCommand::Read;
        break;
    case FlashCommand::Erase2:
        flash_command_ =
            decoded == 0x2AAAU && value == 0x55U ? FlashCommand::Erase3 : FlashCommand::Read;
        break;
    case FlashCommand::Erase3:
        flash_command_ = FlashCommand::Read;
        if (value == 0x30U) {
            begin_flash(
                FlashOperation::Sector, address & ~(kFlashSectorSize - 1U), 0xFFU, kSectorCycles);
        } else if (decoded == 0x5555U && value == 0x10U) {
            begin_flash(FlashOperation::Chip, 0U, 0xFFU, kChipCycles);
        }
        break;
    case FlashCommand::Program:
        break;
    }
}

void Mapper406::clock_cpu() {
    if (flash_cycles_ == 0U) {
        return;
    }
    --flash_cycles_;
    if (flash_cycles_ != 0U) {
        return;
    }
    if (flash_operation_ == FlashOperation::Program) {
        flash_[pending_address_] &= pending_data_;
    } else if (flash_operation_ == FlashOperation::Sector) {
        std::fill_n(flash_.begin() + pending_address_, kFlashSectorSize, 0xFFU);
    } else if (flash_operation_ == FlashOperation::Chip) {
        std::ranges::fill(flash_, 0xFFU);
    }
    flash_operation_ = FlashOperation::None;
    flash_toggle_ = 0U;
}

bool Mapper406::has_flash_storage() const {
    return true;
}

std::span<const Byte> Mapper406::battery_ram() const {
    return flash_;
}

void Mapper406::set_battery_ram(std::span<const Byte> data) {
    if (data.size() == kPrgSize) {
        std::ranges::copy(data, flash_.begin());
    }
}

void Mapper406::save_state(StateWriter& writer) const {
    writer.write(static_cast<std::uint16_t>(kStateTag | submapper_));
    writer.write<std::uint32_t>(kPrgSize);
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(static_cast<Byte>(initial_mirror_));
    writer.write(registers_.bank_select);
    writer.write_array(registers_.banks);
    writer.write(static_cast<Byte>(registers_.mirror));
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_latch);
    writer.write<Byte>((registers_.irq_reload ? 1U : 0U) | (registers_.irq_enabled ? 2U : 0U) |
                       (registers_.irq_pending ? 4U : 0U));
    writer.write_bytes(flash_);
    writer.write(flash_command_);
    writer.write(flash_operation_);
    writer.write<Byte>(flash_id_mode_ ? 1U : 0U);
    writer.write(flash_toggle_);
    writer.write(pending_data_);
    writer.write(pending_address_);
    writer.write(flash_cycles_);
}

void Mapper406::load_state(StateReader& reader) {
    struct NextState {
        Registers registers;
        std::vector<Byte> flash;
        FlashCommand command = FlashCommand::Read;
        FlashOperation operation = FlashOperation::None;
        Byte id_mode = 0U;
        Byte toggle = 0U;
        Byte pending_data = 0U;
        std::uint32_t pending_address = 0U;
        std::uint32_t cycles = 0U;
    } next;

    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto initial_mirror = reader.read<Byte>();
    reader.read(next.registers.bank_select);
    reader.read_array(next.registers.banks);
    const auto mirror = reader.read<Byte>();
    reader.read(next.registers.irq_counter);
    reader.read(next.registers.irq_latch);
    const auto irq = reader.read<Byte>();
    reader.read_bytes(next.flash, kPrgSize);
    reader.read(next.command);
    reader.read(next.operation);
    reader.read(next.id_mode);
    reader.read(next.toggle);
    reader.read(next.pending_data);
    reader.read(next.pending_address);
    reader.read(next.cycles);

    const auto maximum = next.operation == FlashOperation::Program  ? kProgramCycles
                         : next.operation == FlashOperation::Sector ? kSectorCycles
                                                                    : kChipCycles;
    const bool valid_flash =
        next.flash.size() == kPrgSize && next.command <= FlashCommand::Erase3 &&
        next.operation <= FlashOperation::Chip && next.id_mode <= 1U && next.toggle <= 1U &&
        next.pending_address < kPrgSize && next.cycles <= maximum &&
        ((next.cycles == 0U) == (next.operation == FlashOperation::None)) &&
        (next.cycles == 0U || (next.id_mode == 0U && next.command == FlashCommand::Read)) &&
        (next.id_mode == 0U ||
         (next.command == FlashCommand::Read && next.operation == FlashOperation::None)) &&
        (next.cycles != 0U || next.toggle == 0U) &&
        (next.operation != FlashOperation::Sector ||
         (next.pending_address & (kFlashSectorSize - 1U)) == 0U) &&
        (next.operation != FlashOperation::Chip || next.pending_address == 0U);

    if (!reader.ok() || tag != (kStateTag | submapper_) || prg_size != kPrgSize ||
        chr_size != chr_rom_.size() || initial_mirror != static_cast<Byte>(initial_mirror_) ||
        (next.registers.bank_select & 0x38U) != 0U || next.registers.banks[6U] > 0x3FU ||
        next.registers.banks[7U] > 0x3FU || mirror > static_cast<Byte>(MirrorMode::Vertical) ||
        irq > 7U || ((irq & 4U) != 0U && (irq & 2U) == 0U) || !valid_flash) {
        reader.fail();
        return;
    }

    next.registers.mirror = static_cast<MirrorMode>(mirror);
    next.registers.irq_reload = (irq & 1U) != 0U;
    next.registers.irq_enabled = (irq & 2U) != 0U;
    next.registers.irq_pending = (irq & 4U) != 0U;
    registers_ = next.registers;
    flash_ = std::move(next.flash);
    flash_command_ = next.command;
    flash_operation_ = next.operation;
    flash_id_mode_ = next.id_mode != 0U;
    flash_toggle_ = next.toggle;
    pending_data_ = next.pending_data;
    pending_address_ = next.pending_address;
    flash_cycles_ = next.cycles;
}

} // namespace mapperbus::core
