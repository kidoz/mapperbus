#include "core/mappers/fs304.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr Word kEepromWriteCycles = 9000U;
constexpr std::size_t kEepromSize = 512U;
} // namespace

// NESdev mapper 164 r22865, cy2000-3 board trace 20835, and ST M93C66 Rev. 20.
Fs304::Fs304(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), wram_size_(header.is_nes2 ? header.prg_ram_size : 2048U) {
    (void)chr_rom;
    eeprom_.data.fill(0xFFU);
    power_on();
}

Result<void> Fs304::validate_geometry(const INesHeader& header,
                                      std::size_t prg_size,
                                      std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 164 supports submapper 0");
    if (prg_size < 32768U || prg_size > 2097152U || !std::has_single_bit(prg_size) ||
        chr_size != 0U)
        return std::unexpected("Mapper 164 requires power-of-two 32 KiB-2 MiB PRG and no CHR ROM");
    if (!header.has_battery)
        return std::unexpected("Mapper 164 requires persistent 512-byte EEPROM metadata");
    if (header.is_nes2 && (header.prg_nvram_size != kEepromSize || header.chr_ram_size != 8192U ||
                           header.chr_nvram_size != 0U ||
                           (header.prg_ram_size != 0U && header.prg_ram_size != 2048U &&
                            header.prg_ram_size != 8192U)))
        return std::unexpected(
            "Mapper 164 requires 512-byte EEPROM, 8 KiB CHR RAM and 0/2/8 KiB PRG RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 164 requires ordinary H/V CIRAM metadata");
    return {};
}

std::size_t Fs304::prg_bank(Address addr) const {
    const std::size_t outer = static_cast<std::size_t>(bank_high_) << 5U;
    if ((bank_low_ & 0x10U) != 0U)
        return outer | (static_cast<std::size_t>(bank_low_ & 0x0FU) << 1U) | ((addr >> 14U) & 1U);
    if (addr < 0xC000U)
        return outer | ((bank_low_ & 0x20U) >> 1U) | (bank_low_ & 0x0FU);
    if ((bank_low_ & 0x40U) == 0U)
        return outer | 0x1FU;
    return outer | 0x1CU | ((bank_low_ & 1U) << 1U);
}

Byte Fs304::read_prg(Address addr) {
    if (addr >= 0x8000U) {
        if (!maps_prg(addr))
            return 0U;
        const auto offset = prg_bank(addr) * 16384U + (addr & 0x3FFFU);
        return prg_rom_[offset % prg_rom_.size()];
    }
    if (addr >= 0x6000U && wram_size_ != 0U)
        return wram_[(addr & 0x1FFFU) % wram_size_];
    return 0U;
}

void Fs304::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U && wram_size_ != 0U)
        wram_[(addr & 0x1FFFU) % wram_size_] = value;
}

bool Fs304::maps_prg(Address addr) const {
    if (addr >= 0x8000U)
        return (bank_low_ & 0x10U) == 0U || (bank_low_ & 0x40U) != 0U;
    return addr >= 0x6000U && wram_size_ != 0U;
}

Address Fs304::chr_address(Address addr) const {
    addr &= 0x1FFFU;
    if ((bank_low_ & 0x80U) == 0U)
        return addr;
    return static_cast<Address>((addr & 0x0EF7U) | (latched_a0_ ? 0x0008U : 0U) |
                                (latched_a9_ ? 0x1000U : 0U));
}

Byte Fs304::read_chr(Address addr) {
    return chr_ram_[chr_address(addr)];
}

void Fs304::write_chr(Address addr, Byte value) {
    chr_ram_[chr_address(addr)] = value;
}

MirrorMode Fs304::mirror_mode() const {
    if ((bank_low_ & 0x10U) == 0U)
        return MirrorMode::Vertical;
    return (mirror_control_ & 0x80U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
}

bool Fs304::maps_expansion(Address addr) const {
    const auto page = addr & 0xFF00U;
    return (page >= 0x5000U && page <= 0x5300U) || page == 0x5500U;
}

Byte Fs304::read_expansion(Address addr) {
    if ((addr & 0xFF00U) == 0x5500U)
        return eeprom_output() ? 0U : 0x04U;
    return 0U;
}

Byte Fs304::expansion_read_mask(Address addr) const {
    return (addr & 0xFF00U) == 0x5500U ? 0x04U : 0U;
}

void Fs304::write_expansion(Address addr, Byte value) {
    switch (addr & 0xFF00U) {
    case 0x5000U:
        bank_low_ = value;
        break;
    case 0x5100U:
        bank_high_ = value & 0x03U;
        break;
    case 0x5200U:
        microwire_ = value & 0x55U;
        eeprom_set_lines(
            (microwire_ & 0x10U) != 0U, (microwire_ & 0x04U) != 0U, (microwire_ & 0x01U) != 0U);
        break;
    case 0x5300U:
        mirror_control_ = value & 0x80U;
        break;
    default:
        break;
    }
}

void Fs304::observe_cpu_read(Address addr, Byte data) {
    const auto page = addr & 0xFF00U;
    if (page >= 0x5000U && page <= 0x5300U)
        write_expansion(addr, data);
}

bool Fs304::observes_ppu_address() const {
    return true;
}

void Fs304::observe_ppu_address(Address addr) {
    const bool a13 = (addr & 0x2000U) != 0U;
    if (a13 && !previous_a13_) {
        latched_a0_ = (addr & 0x0001U) != 0U;
        latched_a9_ = (addr & 0x0200U) != 0U;
    }
    previous_a13_ = a13;
}

bool Fs304::eeprom_output() const {
    return !eeprom_.chip_select || (eeprom_.busy == 0U && eeprom_.data_out);
}

void Fs304::eeprom_decode_command() {
    const auto op = static_cast<unsigned>((eeprom_.shift >> 9U) & 3U);
    const auto special = static_cast<unsigned>((eeprom_.shift >> 7U) & 3U);
    eeprom_.address = eeprom_.shift & 0x01FFU;
    eeprom_.read_bits = 0U;
    eeprom_.write_data = 0U;
    eeprom_.valid = true;
    switch (op) {
    case 0U:
        eeprom_.phase = EepromState::Phase::Ignore;
        if (special == 0U)
            eeprom_.write_enabled = false;
        else if (special == 3U)
            eeprom_.write_enabled = true;
        else if (special == 1U) {
            eeprom_.phase = EepromState::Phase::WriteData;
            eeprom_.pending = EepromState::Operation::WriteAll;
            eeprom_.bits = 0U;
            eeprom_.shift = 0U;
        } else {
            eeprom_.phase = EepromState::Phase::Erase;
            eeprom_.pending = EepromState::Operation::EraseAll;
        }
        break;
    case 1U:
        eeprom_.phase = EepromState::Phase::WriteData;
        eeprom_.pending = EepromState::Operation::Write;
        eeprom_.bits = 0U;
        eeprom_.shift = 0U;
        break;
    case 2U:
        eeprom_.phase = EepromState::Phase::Read;
        eeprom_.data_out = false;
        break;
    default:
        eeprom_.phase = EepromState::Phase::Erase;
        eeprom_.pending = EepromState::Operation::Erase;
        break;
    }
}

void Fs304::eeprom_set_lines(bool chip_select, bool clock, bool data_in) {
    const bool select_rise = chip_select && !eeprom_.chip_select;
    const bool clock_rise = clock && !eeprom_.clock;
    if (!chip_select) {
        if (eeprom_.chip_select) {
            const bool complete =
                eeprom_.valid &&
                ((eeprom_.phase == EepromState::Phase::WriteData && eeprom_.bits == 8U) ||
                 eeprom_.phase == EepromState::Phase::Erase);
            if (complete && eeprom_.write_enabled &&
                eeprom_.pending != EepromState::Operation::None) {
                eeprom_.pending_address = eeprom_.address;
                eeprom_.pending_data = eeprom_.write_data;
                eeprom_.busy = kEepromWriteCycles;
            } else if (eeprom_.busy == 0U)
                eeprom_.pending = EepromState::Operation::None;
        }
        eeprom_.phase = EepromState::Phase::Idle;
        eeprom_.shift = 0U;
        eeprom_.bits = 0U;
        eeprom_.read_bits = 0U;
        eeprom_.valid = true;
        eeprom_.data_out = true;
        if (eeprom_.busy == 0U)
            eeprom_.pending = EepromState::Operation::None;
    } else if (eeprom_.busy != 0U) {
        eeprom_.phase = EepromState::Phase::Idle;
        eeprom_.shift = 0U;
        eeprom_.bits = 0U;
        eeprom_.data_out = false;
    } else {
        if (select_rise) {
            eeprom_.phase = EepromState::Phase::Idle;
            eeprom_.shift = 0U;
            eeprom_.bits = 0U;
            eeprom_.read_bits = 0U;
            eeprom_.pending = EepromState::Operation::None;
            eeprom_.valid = true;
            eeprom_.data_out = true;
        }
        if (clock_rise) {
            switch (eeprom_.phase) {
            case EepromState::Phase::Idle:
                if (data_in) {
                    eeprom_.phase = EepromState::Phase::Command;
                    eeprom_.shift = 1U;
                    eeprom_.bits = 1U;
                }
                break;
            case EepromState::Phase::Command:
                if (eeprom_.bits < 12U) {
                    eeprom_.shift = static_cast<Word>((eeprom_.shift << 1U) | (data_in ? 1U : 0U));
                    ++eeprom_.bits;
                    if (eeprom_.bits == 12U)
                        eeprom_decode_command();
                } else {
                    eeprom_.valid = false;
                }
                break;
            case EepromState::Phase::Read:
                if (eeprom_.read_bits == 8U) {
                    eeprom_.address = (eeprom_.address + 1U) % kEepromSize;
                    eeprom_.read_bits = 0U;
                }
                eeprom_.data_out =
                    (eeprom_.data[eeprom_.address] & (0x80U >> eeprom_.read_bits)) != 0U;
                ++eeprom_.read_bits;
                break;
            case EepromState::Phase::WriteData:
                if (eeprom_.bits < 8U) {
                    eeprom_.shift = static_cast<Word>((eeprom_.shift << 1U) | (data_in ? 1U : 0U));
                    ++eeprom_.bits;
                    if (eeprom_.bits == 8U)
                        eeprom_.write_data = static_cast<Byte>(eeprom_.shift);
                } else {
                    eeprom_.valid = false;
                }
                break;
            case EepromState::Phase::Erase:
                eeprom_.valid = false;
                break;
            case EepromState::Phase::Ignore:
                break;
            }
        }
    }
    eeprom_.chip_select = chip_select;
    eeprom_.clock = clock;
    eeprom_.data_in = data_in;
}

void Fs304::eeprom_power_on() {
    const auto data = eeprom_.data;
    eeprom_ = EepromState{};
    eeprom_.data = data;
}

void Fs304::clock_cpu() {
    if (eeprom_.busy == 0U)
        return;
    --eeprom_.busy;
    if (eeprom_.busy != 0U)
        return;
    switch (eeprom_.pending) {
    case EepromState::Operation::Write:
        eeprom_.data[eeprom_.pending_address] = eeprom_.pending_data;
        break;
    case EepromState::Operation::Erase:
        eeprom_.data[eeprom_.pending_address] = 0xFFU;
        break;
    case EepromState::Operation::WriteAll:
        eeprom_.data.fill(eeprom_.pending_data);
        break;
    case EepromState::Operation::EraseAll:
        eeprom_.data.fill(0xFFU);
        break;
    case EepromState::Operation::None:
        break;
    }
    eeprom_.pending = EepromState::Operation::None;
    eeprom_.pending_address = 0U;
    eeprom_.pending_data = 0U;
    eeprom_.data_out = true;
}

void Fs304::reset() {
    eeprom_set_lines(false, false, false);
    bank_low_ = 0U;
    bank_high_ = 0U;
    microwire_ = 0U;
    mirror_control_ = 0U;
    latched_a0_ = false;
    latched_a9_ = false;
    previous_a13_ = false;
}

void Fs304::power_on() {
    eeprom_power_on();
    reset();
    wram_.fill(0U);
    chr_ram_.fill(0U);
}

std::span<const Byte> Fs304::battery_ram() const {
    return eeprom_.data;
}

void Fs304::set_battery_ram(std::span<const Byte> data) {
    if (data.size() == eeprom_.data.size())
        std::ranges::copy(data, eeprom_.data.begin());
}

uint16_t Fs304::state_tag() const {
    const auto geometry = std::bit_width(prg_rom_.size() / 32768U) - 1U;
    const auto ram = wram_size_ == 0U ? 0U : (wram_size_ == 2048U ? 1U : 2U);
    return static_cast<uint16_t>(0xA400U | (geometry << 2U) | ram);
}

void Fs304::eeprom_save_state(StateWriter& writer) const {
    writer.write_array(eeprom_.data);
    writer.write(eeprom_.phase);
    writer.write(eeprom_.shift);
    writer.write(eeprom_.bits);
    writer.write(eeprom_.read_bits);
    writer.write(eeprom_.address);
    writer.write(eeprom_.write_data);
    writer.write(static_cast<Byte>(eeprom_.write_enabled));
    writer.write(static_cast<Byte>(eeprom_.valid));
    writer.write(eeprom_.pending);
    writer.write(eeprom_.pending_address);
    writer.write(eeprom_.pending_data);
    writer.write(static_cast<Byte>(eeprom_.chip_select));
    writer.write(static_cast<Byte>(eeprom_.clock));
    writer.write(static_cast<Byte>(eeprom_.data_in));
    writer.write(static_cast<Byte>(eeprom_.data_out));
    writer.write(eeprom_.busy);
}

void Fs304::eeprom_load_state(StateReader& reader, EepromState& state) const {
    reader.read_array(state.data);
    reader.read(state.phase);
    reader.read(state.shift);
    reader.read(state.bits);
    reader.read(state.read_bits);
    reader.read(state.address);
    reader.read(state.write_data);
    const auto write_enabled = reader.read<Byte>();
    const auto valid = reader.read<Byte>();
    reader.read(state.pending);
    reader.read(state.pending_address);
    reader.read(state.pending_data);
    const auto chip_select = reader.read<Byte>();
    const auto clock = reader.read<Byte>();
    const auto data_in = reader.read<Byte>();
    const auto data_out = reader.read<Byte>();
    reader.read(state.busy);
    state.write_enabled = write_enabled != 0U;
    state.valid = valid != 0U;
    state.chip_select = chip_select != 0U;
    state.clock = clock != 0U;
    state.data_in = data_in != 0U;
    state.data_out = data_out != 0U;
    const bool phase_ok =
        (state.phase == EepromState::Phase::Idle && state.bits == 0U) ||
        (state.phase == EepromState::Phase::Command && state.bits >= 1U && state.bits < 12U) ||
        (state.phase == EepromState::Phase::Read && state.bits == 12U) ||
        (state.phase == EepromState::Phase::WriteData && state.bits <= 8U) ||
        (state.phase == EepromState::Phase::Erase && state.bits == 12U) ||
        (state.phase == EepromState::Phase::Ignore && state.bits == 12U);
    if (!reader.ok() || !phase_ok || state.shift > 0x0FFFU || state.read_bits > 8U ||
        state.address >= kEepromSize || state.pending_address >= kEepromSize ||
        write_enabled > 1U || valid > 1U || state.pending > EepromState::Operation::EraseAll ||
        chip_select > 1U || clock > 1U || data_in > 1U || data_out > 1U ||
        state.busy > kEepromWriteCycles ||
        (state.busy != 0U && state.pending == EepromState::Operation::None) ||
        (state.busy == 0U && state.pending != EepromState::Operation::None &&
         state.phase != EepromState::Phase::WriteData &&
         state.phase != EepromState::Phase::Erase) ||
        (state.busy != 0U && state.phase != EepromState::Phase::Idle))
        reader.fail();
}

void Fs304::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(bank_low_);
    writer.write(bank_high_);
    writer.write(microwire_);
    writer.write(mirror_control_);
    writer.write(static_cast<Byte>(latched_a0_));
    writer.write(static_cast<Byte>(latched_a9_));
    writer.write(static_cast<Byte>(previous_a13_));
    writer.write_array(wram_);
    writer.write_array(chr_ram_);
    eeprom_save_state(writer);
}

void Fs304::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto bank_low = reader.read<Byte>();
    const auto bank_high = reader.read<Byte>();
    const auto microwire = reader.read<Byte>();
    const auto mirror_control = reader.read<Byte>();
    const auto latched_a0 = reader.read<Byte>();
    const auto latched_a9 = reader.read<Byte>();
    const auto previous_a13 = reader.read<Byte>();
    std::array<Byte, 8192> wram{};
    std::array<Byte, 8192> chr_ram{};
    reader.read_array(wram);
    reader.read_array(chr_ram);
    EepromState eeprom{};
    eeprom_load_state(reader, eeprom);
    if (!reader.ok() || tag != state_tag() || bank_high > 3U || (microwire & 0xAAU) != 0U ||
        (mirror_control & 0x7FU) != 0U || latched_a0 > 1U || latched_a9 > 1U || previous_a13 > 1U) {
        reader.fail();
        return;
    }
    bank_low_ = bank_low;
    bank_high_ = bank_high;
    microwire_ = microwire;
    mirror_control_ = mirror_control;
    latched_a0_ = latched_a0 != 0U;
    latched_a9_ = latched_a9 != 0U;
    previous_a13_ = previous_a13 != 0U;
    wram_ = wram;
    chr_ram_ = chr_ram;
    eeprom_ = eeprom;
}

} // namespace mapperbus::core
