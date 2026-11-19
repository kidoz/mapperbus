#include "core/mappers/mapper451.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kChrRamSize = 0x004000UZ;
constexpr std::size_t kBankSize = 0x002000UZ;
constexpr std::size_t kFlashSectorSize = 0x010000UZ;
constexpr std::uint16_t kStateTag = 0x4510U;
constexpr std::uint32_t kProgramCycles = 32U;
constexpr std::uint32_t kSectorCycles = 36000U;
constexpr std::uint32_t kChipCycles = 144000U;
constexpr std::uint32_t kMp3BitCycles = 1000U;

} // namespace

// Original implementation from NESdev mapper 451 oldid 24353. Flash timing is
// a deterministic policy; reset has no documented flash or MP3-device input.
Mapper451::Mapper451(const INesHeader& header,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> /*chr_rom*/)
    : flash_(std::move(prg_rom)), initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper451::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 451U) {
        return std::unexpected("Mapper 451 requires mapper ID 451");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 451 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != 0U) {
        return std::unexpected("Mapper 451 requires exactly 512 KiB PRG flash and no CHR ROM");
    }
    if (header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 451 requires exactly 16 KiB volatile CHR RAM");
    }
    if (header.prg_ram_size != 0U || header.prg_nvram_size != 0U || header.has_battery) {
        return std::unexpected("Mapper 451 has no CPU work RAM or battery-backed RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 451 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 451 does not provide trainer RAM");
    }
    return {};
}

std::size_t Mapper451::prg_offset(Address addr) const {
    const unsigned slot = (addr - 0x8000U) / kBankSize;
    const unsigned selectable = 0x10U + (bank_ & 1U) + ((bank_ & 2U) << 2U);
    const unsigned bank = slot == 0U   ? 0U
                          : slot == 1U ? selectable
                          : slot == 2U ? selectable + 0x10U
                                       : 0x30U;
    return static_cast<std::size_t>(bank) * kBankSize + (addr & (kBankSize - 1U));
}

std::size_t Mapper451::chr_offset(Address addr) const {
    return static_cast<std::size_t>(bank_ & 1U) * kBankSize + (addr & (kBankSize - 1U));
}

Byte Mapper451::read_prg(Address addr) {
    if (addr == 0x6003U) {
        return mp3_status();
    }
    if (addr < 0x8000U) {
        return 0U;
    }

    const auto address = prg_offset(addr);
    if (flash_cycles_ != 0U) {
        const Byte value = static_cast<Byte>((flash_operation_ == FlashOperation::Program
                                                  ? (~static_cast<unsigned>(pending_data_) & 0x80U)
                                                  : 0U) |
                                             (flash_toggle_ == 0U ? 0x40U : 0U));
        flash_toggle_ ^= 1U;
        return value;
    }
    if (flash_id_mode_) {
        if (address == 0U) {
            return 0x37U;
        }
        if (address == 1U) {
            return 0x86U;
        }
        return 0xFFU;
    }
    return flash_[address];
}

void Mapper451::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }

    write_flash(prg_offset(addr), addr, value);
    switch (addr & 0xE000U) {
    case 0xA000U:
        mirror_ = (addr & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xC000U: {
        const Byte encoded = static_cast<Byte>(addr);
        irq_latch_ = static_cast<Byte>(encoded - 1U);
        irq_counter_ = 0U;
        irq_reload_ = true;
        irq_enabled_ = encoded != 0xFFU;
        if (!irq_enabled_) {
            irq_pending_ = false;
        }
        break;
    }
    case 0xE000U:
        bank_ = addr & 3U;
        break;
    default:
        break;
    }
}

void Mapper451::observe_cpu_read(Address addr, Byte /*data*/) {
    if (addr == 0x6003U) {
        read_mp3();
    }
}

bool Mapper451::maps_prg(Address addr) const {
    return addr >= 0x8000U || addr == 0x6003U;
}

Byte Mapper451::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper451::read_chr(Address addr) {
    return chr_ram_[chr_offset(addr)];
}

void Mapper451::write_chr(Address addr, Byte value) {
    chr_ram_[chr_offset(addr)] = value;
}

MirrorMode Mapper451::mirror_mode() const {
    return mirror_;
}

void Mapper451::power_on() {
    reset();
    chr_ram_.fill(0U);
    mp3_phase_ = Mp3Phase::Idle;
    mp3_shift_ = 0U;
    mp3_bit_count_ = 0U;
    mp3_cycles_ = 0U;
    mp3_status_index_ = 0U;
    mp3_volume_ = 0U;
    mp3_equalizer_ = 0U;
    mp3_folder_ = 0U;
    mp3_song_ = 0U;
    mp3_playing_ = false;
}

void Mapper451::reset() {
    bank_ = 0U;
    mirror_ = initial_mirror_;
    irq_counter_ = 0U;
    irq_latch_ = 0U;
    irq_reload_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
}

bool Mapper451::irq_pending() const {
    return irq_pending_;
}

void Mapper451::clock_irq_counter() {
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

void Mapper451::begin_flash(FlashOperation operation,
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

void Mapper451::write_flash(std::size_t address, Address bus_address, Byte value) {
    if (flash_cycles_ != 0U) {
        return;
    }
    const auto decoded = bus_address & 0x7FFFU;
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

void Mapper451::clock_flash() {
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

void Mapper451::clock_cpu() {
    clock_flash();
    clock_mp3();
}

Byte Mapper451::mp3_status() const {
    switch (mp3_status_index_) {
    case 1U:
        return 0xA0U;
    case 2U:
        return 0x20U;
    case 3U:
        return static_cast<Byte>(mp3_equalizer_ << 4U);
    case 4U:
        return static_cast<Byte>((mp3_volume_ & 0x0FU) << 4U);
    case 5U:
        return mp3_volume_ & 0x10U;
    case 6U:
        return static_cast<Byte>(mp3_folder_ << 4U);
    default:
        return 0U;
    }
}

void Mapper451::clock_mp3() {
    if (mp3_phase_ != Mp3Phase::Command) {
        return;
    }
    ++mp3_cycles_;
    if (mp3_cycles_ <= kMp3BitCycles) {
        return;
    }
    mp3_cycles_ = 0U;
    mp3_shift_ = static_cast<Byte>((mp3_shift_ << 1U) | 1U);
    ++mp3_bit_count_;
    if (mp3_bit_count_ == 8U) {
        mp3_phase_ = Mp3Phase::Terminate;
    }
}

void Mapper451::read_mp3() {
    switch (mp3_phase_) {
    case Mp3Phase::Idle:
        mp3_phase_ = Mp3Phase::Command;
        mp3_shift_ = 0U;
        mp3_bit_count_ = 0U;
        mp3_cycles_ = 0U;
        mp3_status_index_ = 0U;
        break;
    case Mp3Phase::Command:
        mp3_shift_ = static_cast<Byte>(mp3_shift_ << 1U);
        ++mp3_bit_count_;
        mp3_cycles_ = 0U;
        if (mp3_bit_count_ == 8U) {
            mp3_phase_ = Mp3Phase::Terminate;
        }
        break;
    case Mp3Phase::Terminate:
        execute_mp3_command();
        break;
    case Mp3Phase::Status:
        if (mp3_status_index_ == 7U) {
            mp3_status_index_ = 0U;
            mp3_phase_ = Mp3Phase::Idle;
        } else {
            ++mp3_status_index_;
        }
        break;
    }
}

void Mapper451::execute_mp3_command() {
    const Byte command = mp3_shift_;
    if (command >= 1U && command <= 0xBFU) {
        mp3_song_ = command;
        mp3_playing_ = true;
    } else if (command >= 0xC0U && command <= 0xDEU) {
        mp3_volume_ = command - 0xC0U;
    } else if (command >= 0xE0U && command <= 0xE5U) {
        mp3_equalizer_ = command - 0xE0U;
    } else if (command >= 0xF0U && command <= 0xFCU) {
        mp3_folder_ = command - 0xF0U;
    } else if (command == 0xFFU) {
        mp3_song_ = 0U;
        mp3_playing_ = false;
    }

    mp3_shift_ = 0U;
    mp3_bit_count_ = 0U;
    mp3_cycles_ = 0U;
    mp3_status_index_ = 0U;
    mp3_phase_ = command == 0U ? Mp3Phase::Status : Mp3Phase::Idle;
}

bool Mapper451::has_flash_storage() const {
    return true;
}

std::span<const Byte> Mapper451::battery_ram() const {
    return flash_;
}

void Mapper451::set_battery_ram(std::span<const Byte> data) {
    if (data.size() == kPrgSize) {
        std::ranges::copy(data, flash_.begin());
    }
}

std::uint16_t Mapper451::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper451::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(flash_.size()));
    writer.write<std::uint32_t>(kChrRamSize);
    writer.write(bank_);
    writer.write(mirror_);
    writer.write(irq_counter_);
    writer.write(irq_latch_);
    writer.write<Byte>((irq_reload_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write_bytes(flash_);
    writer.write(flash_command_);
    writer.write(flash_operation_);
    writer.write<Byte>(flash_id_mode_ ? 1U : 0U);
    writer.write(flash_toggle_);
    writer.write(pending_data_);
    writer.write(pending_address_);
    writer.write(flash_cycles_);
    writer.write_array(chr_ram_);
    writer.write(mp3_phase_);
    writer.write(mp3_shift_);
    writer.write(mp3_bit_count_);
    writer.write(mp3_cycles_);
    writer.write(mp3_status_index_);
    writer.write(mp3_volume_);
    writer.write(mp3_equalizer_);
    writer.write(mp3_folder_);
    writer.write(mp3_song_);
    writer.write<Byte>(mp3_playing_ ? 1U : 0U);
}

void Mapper451::load_state(StateReader& reader) {
    struct NextState {
        Byte bank = 0U;
        MirrorMode mirror = MirrorMode::Vertical;
        Byte irq_counter = 0U;
        Byte irq_latch = 0U;
        Byte irq = 0U;
        std::vector<Byte> flash;
        FlashCommand command = FlashCommand::Read;
        FlashOperation operation = FlashOperation::None;
        Byte id_mode = 0U;
        Byte toggle = 0U;
        Byte pending_data = 0U;
        std::uint32_t pending_address = 0U;
        std::uint32_t flash_cycles = 0U;
        std::array<Byte, kChrRamSize> chr_ram{};
        Mp3Phase mp3_phase = Mp3Phase::Idle;
        Byte mp3_shift = 0U;
        Byte mp3_bit_count = 0U;
        std::uint32_t mp3_cycles = 0U;
        Byte mp3_status_index = 0U;
        Byte mp3_volume = 0U;
        Byte mp3_equalizer = 0U;
        Byte mp3_folder = 0U;
        Byte mp3_song = 0U;
        Byte mp3_playing = 0U;
    } next;

    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    reader.read(next.bank);
    const auto mirror = reader.read<Byte>();
    reader.read(next.irq_counter);
    reader.read(next.irq_latch);
    reader.read(next.irq);
    reader.read_bytes(next.flash, kPrgSize);
    reader.read(next.command);
    reader.read(next.operation);
    reader.read(next.id_mode);
    reader.read(next.toggle);
    reader.read(next.pending_data);
    reader.read(next.pending_address);
    reader.read(next.flash_cycles);
    reader.read_array(next.chr_ram);
    reader.read(next.mp3_phase);
    reader.read(next.mp3_shift);
    reader.read(next.mp3_bit_count);
    reader.read(next.mp3_cycles);
    reader.read(next.mp3_status_index);
    reader.read(next.mp3_volume);
    reader.read(next.mp3_equalizer);
    reader.read(next.mp3_folder);
    reader.read(next.mp3_song);
    reader.read(next.mp3_playing);

    const auto maximum = next.operation == FlashOperation::Program  ? kProgramCycles
                         : next.operation == FlashOperation::Sector ? kSectorCycles
                                                                    : kChipCycles;
    const bool valid_flash =
        next.flash.size() == kPrgSize && next.command <= FlashCommand::Erase3 &&
        next.operation <= FlashOperation::Chip && next.id_mode <= 1U && next.toggle <= 1U &&
        next.pending_address < kPrgSize && next.flash_cycles <= maximum &&
        ((next.flash_cycles == 0U) == (next.operation == FlashOperation::None)) &&
        (next.flash_cycles == 0U || (next.id_mode == 0U && next.command == FlashCommand::Read)) &&
        (next.id_mode == 0U ||
         (next.command == FlashCommand::Read && next.operation == FlashOperation::None)) &&
        (next.flash_cycles != 0U || next.toggle == 0U) &&
        (next.operation != FlashOperation::Sector ||
         (next.pending_address & (kFlashSectorSize - 1U)) == 0U) &&
        (next.operation != FlashOperation::Chip || next.pending_address == 0U);
    const bool valid_mp3 =
        next.mp3_phase <= Mp3Phase::Status && next.mp3_bit_count <= 8U &&
        next.mp3_cycles <= kMp3BitCycles && next.mp3_status_index < 8U && next.mp3_volume <= 30U &&
        next.mp3_equalizer <= 5U && next.mp3_folder <= 12U && next.mp3_song <= 191U &&
        next.mp3_playing <= 1U &&
        (next.mp3_phase != Mp3Phase::Idle ||
         (next.mp3_shift == 0U && next.mp3_bit_count == 0U && next.mp3_cycles == 0U &&
          next.mp3_status_index == 0U)) &&
        (next.mp3_phase != Mp3Phase::Command || next.mp3_bit_count < 8U) &&
        (next.mp3_phase != Mp3Phase::Terminate || next.mp3_bit_count == 8U) &&
        (next.mp3_phase != Mp3Phase::Status ||
         (next.mp3_shift == 0U && next.mp3_bit_count == 0U && next.mp3_cycles == 0U)) &&
        (next.mp3_playing == 0U ? next.mp3_song == 0U : next.mp3_song != 0U);

    if (!reader.ok() || tag != state_tag() || prg_size != kPrgSize || chr_size != kChrRamSize ||
        next.bank > 3U || mirror > static_cast<Byte>(MirrorMode::Vertical) || next.irq > 7U ||
        ((next.irq & 4U) != 0U && (next.irq & 2U) == 0U) || !valid_flash || !valid_mp3) {
        reader.fail();
        return;
    }

    bank_ = next.bank;
    mirror_ = static_cast<MirrorMode>(mirror);
    irq_counter_ = next.irq_counter;
    irq_latch_ = next.irq_latch;
    irq_reload_ = (next.irq & 1U) != 0U;
    irq_enabled_ = (next.irq & 2U) != 0U;
    irq_pending_ = (next.irq & 4U) != 0U;
    flash_ = std::move(next.flash);
    flash_command_ = next.command;
    flash_operation_ = next.operation;
    flash_id_mode_ = next.id_mode != 0U;
    flash_toggle_ = next.toggle;
    pending_data_ = next.pending_data;
    pending_address_ = next.pending_address;
    flash_cycles_ = next.flash_cycles;
    chr_ram_ = next.chr_ram;
    mp3_phase_ = next.mp3_phase;
    mp3_shift_ = next.mp3_shift;
    mp3_bit_count_ = next.mp3_bit_count;
    mp3_cycles_ = next.mp3_cycles;
    mp3_status_index_ = next.mp3_status_index;
    mp3_volume_ = next.mp3_volume;
    mp3_equalizer_ = next.mp3_equalizer;
    mp3_folder_ = next.mp3_folder;
    mp3_song_ = next.mp3_song;
    mp3_playing_ = next.mp3_playing != 0U;
}

} // namespace mapperbus::core
