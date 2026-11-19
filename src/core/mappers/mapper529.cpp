#include "core/mappers/mapper529.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x080000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::uint16_t kStateTag = 0x5290U;
constexpr Word kEepromWriteCycles = 9000U;
constexpr Byte kPrgBankMask = 0x1FU;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 529 oldid 19170.
// Reset values are unspecified: use deterministic VRC4 defaults, retain the
// independently powered EEPROM, and clear volatile CHR RAM only on power-on.
Mapper529::Mapper529(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : initial_mirror_(header.mirror_mode), has_eeprom_(header.prg_nvram_size == kEepromSize),
      prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    eeprom_data_.fill(0xFFU);
    power_on();
}

Result<void> Mapper529::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 529U) {
        return std::unexpected("Mapper 529 requires mapper ID 529");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 529 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        (chr_size != 0U &&
         (!std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxChrSize))) {
        return std::unexpected(
            "Mapper 529 requires power-of-two 32-512 KiB PRG and optional 8-512 KiB CHR ROM");
    }
    if (header.prg_ram_size != 0U ||
        (header.prg_nvram_size != 0U && header.prg_nvram_size != kEepromSize) ||
        header.has_battery != (header.prg_nvram_size == kEepromSize)) {
        return std::unexpected("Mapper 529 supports only optional 256-byte battery 93C56 EEPROM");
    }
    if (header.chr_nvram_size != 0U ||
        (chr_size == 0U ? header.chr_ram_size != 0x2000U : header.chr_ram_size != 0U)) {
        return std::unexpected("Mapper 529 requires 8 KiB volatile CHR RAM only without CHR ROM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 529 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 529 does not provide trainer RAM");
    }
    return {};
}

bool Mapper529::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper529::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper529::read_prg(Address addr) {
    if (!maps_prg(addr) || prg_rom_.empty()) {
        return 0U;
    }
    if (addr >= 0xC000U) {
        return prg_rom_[prg_rom_.size() - kPrgBankSize + (addr & 0x3FFFU)];
    }
    const auto offset =
        static_cast<std::size_t>(vrc_.prg_bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper529::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
    if (has_eeprom_ && (addr & 0x8800U) == 0x8800U) {
        set_microwire((value & 4U) != 0U, (value & 2U) != 0U, (value & 1U) != 0U);
    }

    const auto group = static_cast<unsigned>(addr >> 12U);
    const auto port = static_cast<unsigned>((addr >> 2U) & 3U);
    if (group == 9U && port == 0U) {
        constexpr std::array kModes = {MirrorMode::Vertical,
                                       MirrorMode::Horizontal,
                                       MirrorMode::SingleLower,
                                       MirrorMode::SingleUpper};
        vrc_.mirror = kModes[value & 3U];
    } else if (group == 10U) {
        vrc_.prg_bank = value & kPrgBankMask;
    } else if (group >= 11U && group <= 14U) {
        auto& bank = vrc_.chr_banks[(group - 11U) * 2U + (port >> 1U)];
        if ((port & 1U) == 0U) {
            bank = (bank & 0x1F0U) | (value & 0x0FU);
        } else {
            bank = (bank & 0x00FU) | ((value & 0x1FU) << 4U);
        }
    } else if (group == 15U) {
        switch (port) {
        case 0U:
            vrc_.irq_latch = (vrc_.irq_latch & 0xF0U) | (value & 0x0FU);
            break;
        case 1U:
            vrc_.irq_latch = (vrc_.irq_latch & 0x0FU) | ((value & 0x0FU) << 4U);
            break;
        case 2U:
            irq_control(value);
            break;
        case 3U:
            vrc_.irq_pending = 0U;
            vrc_.irq_enabled = vrc_.irq_control & 1U;
            break;
        default:
            break;
        }
    }
}

Byte Mapper529::read_chr(Address addr) {
    if (addr >= 0x2000U) {
        return 0U;
    }
    const auto bank = vrc_.chr_banks[(addr >> 10U) & 7U];
    const auto offset = static_cast<std::size_t>(bank) * kChrBankSize + (addr & 0x03FFU);
    if (!chr_rom_.empty()) {
        return chr_rom_[offset % chr_rom_.size()];
    }
    return chr_ram_[offset % chr_ram_.size()];
}

void Mapper529::write_chr(Address addr, Byte value) {
    if (addr >= 0x2000U || !chr_rom_.empty()) {
        return;
    }
    const auto bank = vrc_.chr_banks[(addr >> 10U) & 7U];
    const auto offset = static_cast<std::size_t>(bank) * kChrBankSize + (addr & 0x03FFU);
    chr_ram_[offset % chr_ram_.size()] = value;
}

MirrorMode Mapper529::mirror_mode() const {
    return vrc_.mirror;
}

bool Mapper529::maps_expansion(Address addr) const {
    return has_eeprom_ && (addr & 0xF000U) == 0x5000U;
}

Byte Mapper529::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0x01U : 0xFFU;
}

Byte Mapper529::read_expansion(Address addr) {
    if (!maps_expansion(addr)) {
        return 0U;
    }
    if (eeprom_.chip_select == 0U) {
        return 1U;
    }
    return eeprom_.phase == EepromPhase::Busy ? 0U : eeprom_.output;
}

void Mapper529::irq_control(Byte value) {
    vrc_.irq_control = value & 7U;
    vrc_.irq_enabled = (value >> 1U) & 1U;
    vrc_.irq_pending = 0U;
    vrc_.irq_prescaler = 341U;
    if (vrc_.irq_enabled != 0U) {
        vrc_.irq_counter = vrc_.irq_latch;
    }
}

void Mapper529::irq_tick() {
    if (vrc_.irq_counter == 0xFFU) {
        vrc_.irq_counter = vrc_.irq_latch;
        vrc_.irq_pending = 1U;
    } else {
        ++vrc_.irq_counter;
    }
}

bool Mapper529::irq_pending() const {
    return vrc_.irq_pending != 0U;
}

void Mapper529::acknowledge_irq() {
    vrc_.irq_pending = 0U;
}

void Mapper529::clock_cpu() {
    clock_microwire();
    if (vrc_.irq_enabled == 0U) {
        return;
    }
    if ((vrc_.irq_control & 4U) != 0U) {
        irq_tick();
    } else if (vrc_.irq_prescaler <= 3U) {
        vrc_.irq_prescaler += 338U;
        irq_tick();
    } else {
        vrc_.irq_prescaler -= 3U;
    }
}

Word Mapper529::read_eeprom_word(Byte address) const {
    const auto offset = static_cast<std::size_t>(address & 0x7FU) * 2U;
    return static_cast<Word>(eeprom_data_[offset] |
                             (static_cast<Word>(eeprom_data_[offset + 1U]) << 8U));
}

void Mapper529::write_eeprom_word(Byte address, Word value) {
    const auto offset = static_cast<std::size_t>(address & 0x7FU) * 2U;
    eeprom_data_[offset] = static_cast<Byte>(value);
    eeprom_data_[offset + 1U] = static_cast<Byte>(value >> 8U);
}

void Mapper529::set_microwire(bool chip_select, bool clock, bool input) {
    const bool select_falling = eeprom_.chip_select != 0U && !chip_select;
    const bool clock_rising = eeprom_.clock == 0U && clock;
    eeprom_.input = input ? 1U : 0U;

    if (select_falling) {
        if (eeprom_.phase == EepromPhase::WaitChipSelect) {
            begin_microwire_write();
        } else if (eeprom_.phase != EepromPhase::Busy) {
            eeprom_.phase = EepromPhase::Command;
            eeprom_.command = 0U;
            eeprom_.command_bits = 0U;
            eeprom_.data_bits = 0U;
            eeprom_.output = 1U;
        }
    } else if (chip_select && clock_rising && eeprom_.phase != EepromPhase::Busy) {
        switch (eeprom_.phase) {
        case EepromPhase::Command:
            eeprom_.command = static_cast<std::uint16_t>((eeprom_.command << 1U) |
                                                         (eeprom_.input != 0U ? 1U : 0U));
            if (++eeprom_.command_bits == 10U) {
                execute_microwire_command();
            }
            break;
        case EepromPhase::Read:
            if (eeprom_.data_bits == 16U) {
                eeprom_.data_bits = 0U;
                eeprom_.shift = read_eeprom_word(eeprom_.address);
            }
            eeprom_.output = (eeprom_.shift & (0x8000U >> eeprom_.data_bits)) != 0U ? 1U : 0U;
            if (++eeprom_.data_bits == 16U) {
                eeprom_.address = (eeprom_.address + 1U) & 0x7FU;
            }
            break;
        case EepromPhase::Write:
            eeprom_.shift = static_cast<std::uint16_t>((eeprom_.shift << 1U) | eeprom_.input);
            if (++eeprom_.data_bits == 16U) {
                eeprom_.pending_data = eeprom_.shift;
                eeprom_.pending_address = eeprom_.address;
                eeprom_.pending = ((eeprom_.command >> 7U) & 3U) == 1U ? EepromOperation::Write
                                                                       : EepromOperation::WriteAll;
                eeprom_.phase = EepromPhase::WaitChipSelect;
                eeprom_.output = 1U;
            }
            break;
        default:
            break;
        }
    }
    eeprom_.chip_select = chip_select ? 1U : 0U;
    eeprom_.clock = clock ? 1U : 0U;
}

void Mapper529::execute_microwire_command() {
    eeprom_.address = eeprom_.command & 0x7FU;
    eeprom_.data_bits = 0U;
    eeprom_.shift = 0U;
    if ((eeprom_.command & 0x0200U) == 0U) {
        eeprom_.phase = EepromPhase::Ignore;
        eeprom_.output = 1U;
        return;
    }

    const auto opcode = (eeprom_.command >> 7U) & 3U;
    if (opcode == 2U) {
        eeprom_.shift = read_eeprom_word(eeprom_.address);
        eeprom_.phase = EepromPhase::Read;
        eeprom_.output = 0U;
        return;
    }
    if (opcode == 0U) {
        switch (eeprom_.address >> 5U) {
        case 0U:
            eeprom_.write_enabled = 0U;
            eeprom_.phase = EepromPhase::Ignore;
            return;
        case 1U:
            break;
        case 2U:
            if (eeprom_.write_enabled) {
                eeprom_.pending = EepromOperation::EraseAll;
                eeprom_.phase = EepromPhase::WaitChipSelect;
            } else {
                eeprom_.phase = EepromPhase::Ignore;
            }
            return;
        default:
            eeprom_.write_enabled = 1U;
            eeprom_.phase = EepromPhase::Ignore;
            return;
        }
    }
    if (!eeprom_.write_enabled) {
        eeprom_.phase = EepromPhase::Ignore;
        eeprom_.output = 1U;
        return;
    }
    if (opcode == 1U || opcode == 3U) {
        if (opcode == 1U) {
            eeprom_.phase = EepromPhase::Write;
        } else {
            eeprom_.pending = EepromOperation::Erase;
            eeprom_.pending_address = eeprom_.address;
            eeprom_.phase = EepromPhase::WaitChipSelect;
        }
        return;
    }
    eeprom_.phase = EepromPhase::Write;
}

void Mapper529::begin_microwire_write() {
    eeprom_.phase = EepromPhase::Busy;
    eeprom_.busy = kEepromWriteCycles;
    eeprom_.output = 0U;
}

void Mapper529::clock_microwire() {
    if (eeprom_.phase != EepromPhase::Busy) {
        return;
    }
    if (--eeprom_.busy != 0U) {
        return;
    }
    switch (eeprom_.pending) {
    case EepromOperation::Write:
        write_eeprom_word(eeprom_.pending_address, eeprom_.pending_data);
        break;
    case EepromOperation::Erase:
        write_eeprom_word(eeprom_.pending_address, 0xFFFFU);
        break;
    case EepromOperation::WriteAll:
        for (Byte address = 0U; address < 0x80U; ++address) {
            write_eeprom_word(address, eeprom_.pending_data);
        }
        break;
    case EepromOperation::EraseAll:
        eeprom_data_.fill(0xFFU);
        break;
    default:
        break;
    }
    eeprom_.pending = EepromOperation::None;
    eeprom_.phase = EepromPhase::Command;
    eeprom_.command = 0U;
    eeprom_.command_bits = 0U;
    eeprom_.data_bits = 0U;
    eeprom_.output = 1U;
}

void Mapper529::reset() {
    vrc_ = VrcState{};
    vrc_.mirror = initial_mirror_;
}

void Mapper529::power_on() {
    reset();
    chr_ram_.fill(0U);
    eeprom_ = EepromState{};
}

std::span<const Byte> Mapper529::battery_ram() const {
    return has_eeprom_ ? std::span<const Byte>{eeprom_data_} : std::span<const Byte>{};
}

void Mapper529::set_battery_ram(std::span<const Byte> data) {
    if (has_eeprom_ && data.size() == eeprom_data_.size()) {
        std::ranges::copy(data, eeprom_data_.begin());
    }
}

void Mapper529::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(static_cast<Byte>(initial_mirror_));
    writer.write<Byte>(has_eeprom_ ? 1U : 0U);
    writer.write(vrc_.prg_bank);
    writer.write_array(vrc_.chr_banks);
    writer.write(vrc_.mirror);
    writer.write(vrc_.irq_latch);
    writer.write(vrc_.irq_counter);
    writer.write(vrc_.irq_prescaler);
    writer.write(vrc_.irq_control);
    writer.write(vrc_.irq_enabled);
    writer.write(vrc_.irq_pending);
    writer.write(eeprom_.chip_select);
    writer.write(eeprom_.clock);
    writer.write(eeprom_.input);
    writer.write(eeprom_.output);
    writer.write(eeprom_.write_enabled);
    writer.write(eeprom_.phase);
    writer.write(eeprom_.pending);
    writer.write(eeprom_.command);
    writer.write(eeprom_.command_bits);
    writer.write(eeprom_.address);
    writer.write(eeprom_.shift);
    writer.write(eeprom_.data_bits);
    writer.write(eeprom_.busy);
    writer.write(eeprom_.pending_data);
    writer.write(eeprom_.pending_address);
    if (chr_rom_.empty()) {
        writer.write_array(chr_ram_);
    }
    if (has_eeprom_) {
        writer.write_array(eeprom_data_);
    }
}

void Mapper529::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto initial_mirror = reader.read<Byte>();
    const auto eeprom_present = reader.read<Byte>();
    VrcState next_vrc;
    reader.read(next_vrc.prg_bank);
    reader.read_array(next_vrc.chr_banks);
    reader.read(next_vrc.mirror);
    reader.read(next_vrc.irq_latch);
    reader.read(next_vrc.irq_counter);
    reader.read(next_vrc.irq_prescaler);
    reader.read(next_vrc.irq_control);
    reader.read(next_vrc.irq_enabled);
    reader.read(next_vrc.irq_pending);
    EepromState next_eeprom;
    reader.read(next_eeprom.chip_select);
    reader.read(next_eeprom.clock);
    reader.read(next_eeprom.input);
    reader.read(next_eeprom.output);
    reader.read(next_eeprom.write_enabled);
    reader.read(next_eeprom.phase);
    reader.read(next_eeprom.pending);
    reader.read(next_eeprom.command);
    reader.read(next_eeprom.command_bits);
    reader.read(next_eeprom.address);
    reader.read(next_eeprom.shift);
    reader.read(next_eeprom.data_bits);
    reader.read(next_eeprom.busy);
    reader.read(next_eeprom.pending_data);
    reader.read(next_eeprom.pending_address);
    std::array<Byte, 0x2000> chr_ram{};
    if (chr_rom_.empty()) {
        reader.read_array(chr_ram);
    }
    std::array<Byte, kEepromSize> eeprom_data{};
    if (has_eeprom_) {
        reader.read_array(eeprom_data);
    }

    const bool valid_irq = next_vrc.irq_prescaler >= 1U && next_vrc.irq_prescaler <= 341U &&
                           next_vrc.irq_control <= 7U && next_vrc.irq_enabled <= 1U &&
                           next_vrc.irq_pending <= 1U &&
                           (next_vrc.irq_pending == 0U || next_vrc.irq_enabled != 0U);
    const bool valid_eeprom_flags = next_eeprom.chip_select <= 1U && next_eeprom.clock <= 1U &&
                                    next_eeprom.input <= 1U && next_eeprom.output <= 1U &&
                                    next_eeprom.write_enabled <= 1U;
    const bool valid_eeprom_protocol =
        next_eeprom.phase <= EepromPhase::Busy &&
        next_eeprom.pending <= EepromOperation::EraseAll && next_eeprom.command <= 0x03FFU &&
        next_eeprom.command_bits <= 10U && next_eeprom.address < 0x80U &&
        next_eeprom.data_bits <= 16U && next_eeprom.busy <= kEepromWriteCycles &&
        next_eeprom.pending_address < 0x80U &&
        ((next_eeprom.phase == EepromPhase::Busy) == (next_eeprom.busy != 0U)) &&
        ((next_eeprom.phase == EepromPhase::WaitChipSelect ||
          next_eeprom.phase == EepromPhase::Busy) ==
         (next_eeprom.pending != EepromOperation::None));
    const bool valid =
        reader.ok() && reader.remaining() == 0U && tag == kStateTag &&
        prg_size == prg_rom_.size() && chr_size == chr_rom_.size() &&
        initial_mirror == static_cast<Byte>(initial_mirror_) && eeprom_present <= 1U &&
        (eeprom_present != 0U) == has_eeprom_ && next_vrc.prg_bank <= kPrgBankMask &&
        std::ranges::all_of(next_vrc.chr_banks,
                            [](std::uint16_t bank) { return bank <= 0x01FFU; }) &&
        next_vrc.mirror != MirrorMode::FourScreen && next_vrc.mirror <= MirrorMode::SingleUpper &&
        valid_irq && valid_eeprom_flags && valid_eeprom_protocol &&
        (has_eeprom_ || next_eeprom == EepromState{});
    if (!valid) {
        reader.fail();
        return;
    }

    vrc_ = next_vrc;
    chr_ram_ = chr_ram;
    if (has_eeprom_) {
        eeprom_ = next_eeprom;
        eeprom_data_ = eeprom_data;
    }
}

} // namespace mapperbus::core
