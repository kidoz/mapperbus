#include "core/mappers/mapper183.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

INesHeader vrc4e_header(const INesHeader& header) {
    auto vrc = header;
    vrc.mapper_number = 23U;
    vrc.submapper = 2U;
    vrc.is_nes2 = true;
    vrc.has_battery = false;
    vrc.prg_ram_size = 0U;
    vrc.prg_nvram_size = 0U;
    vrc.chr_ram_size = 0U;
    vrc.chr_nvram_size = 0U;
    return vrc;
}

struct VrcState {
    std::array<Byte, 3> prg_banks{};
    std::array<uint16_t, 8> chr_banks{};
    MirrorMode mirror = MirrorMode::Vertical;
    Byte mode = 0U;
    Byte microwire = 0U;
    uint16_t irq_latch = 0U;
    uint16_t irq_counter = 0U;
    uint16_t irq_prescaler = 341U;
    Byte irq_control = 0U;
    Byte irq_enabled = 0U;
    Byte irq_pending = 0U;
    std::array<Byte, 8192> unused_chr_ram{};
};

VrcState read_vrc_state(StateReader& reader, uint64_t& prg_ram_size) {
    VrcState state;
    reader.read_array(state.prg_banks);
    reader.read_array(state.chr_banks);
    reader.read(state.mirror);
    reader.read(state.mode);
    reader.read(state.microwire);
    reader.read(state.irq_latch);
    reader.read(state.irq_counter);
    reader.read(state.irq_prescaler);
    reader.read(state.irq_control);
    reader.read(state.irq_enabled);
    reader.read(state.irq_pending);
    prg_ram_size = reader.read<uint64_t>();
    reader.read_array(state.unused_chr_ram);
    return state;
}

void write_vrc_state(StateWriter& writer, const VrcState& state) {
    writer.write_array(state.prg_banks);
    writer.write_array(state.chr_banks);
    writer.write(state.mirror);
    writer.write(state.mode);
    writer.write(state.microwire);
    writer.write(state.irq_latch);
    writer.write(state.irq_counter);
    writer.write(state.irq_prescaler);
    writer.write(state.irq_control);
    writer.write(state.irq_enabled);
    writer.write(state.irq_pending);
    writer.write<uint64_t>(0U);
    writer.write_array(state.unused_chr_ram);
}

bool valid_vrc_state(const VrcState& state, uint64_t prg_ram_size) {
    return state.prg_banks[0U] <= 31U && state.prg_banks[1U] <= 31U && state.prg_banks[2U] == 0U &&
           std::ranges::all_of(state.chr_banks, [](uint16_t bank) { return bank <= 511U; }) &&
           state.mirror <= MirrorMode::SingleUpper && state.mirror != MirrorMode::FourScreen &&
           state.mode <= 3U && state.microwire == 0U && state.irq_latch == 0U &&
           state.irq_counter == 0U && state.irq_prescaler == 341U && state.irq_control == 0U &&
           state.irq_enabled == 0U && state.irq_pending == 0U && prg_ram_size == 0U &&
           std::ranges::all_of(state.unused_chr_ram, [](Byte value) { return value == 0U; });
}

} // namespace

// NESdev mapper 183 r24192, VRC4 r24169 and VRC IRQ r22720. The additional
// 74161 write latch uses CPU A3-A0, not write data. Reset values are deterministic
// emulator policy; the article does not establish the latch's electrical reset input.
Mapper183::Mapper183(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), prg_size_(prg_rom_.size()), chr_size_(chr_rom.size()),
      vrc_(vrc4e_header(header), prg_rom_, std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper183::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 183 supports submapper 0");
    if (prg_size < 32768U || prg_size > 262144U || !std::has_single_bit(prg_size) ||
        chr_size < 8192U || chr_size > 524288U || !std::has_single_bit(chr_size))
        return std::unexpected(
            "Mapper 183 requires power-of-two 32-256 KiB PRG and 8-512 KiB CHR ROM");
    if (header.has_battery ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
                            header.chr_ram_size != 0U || header.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 183 requires ROM-only memory with no battery");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 183 supports H/V startup mirroring");
    return {};
}

Byte Mapper183::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        const auto offset = low_prg_bank_ * 8192U + (addr & 0x1FFFU);
        return prg_rom_[offset % prg_rom_.size()];
    }
    return vrc_.read_prg(addr);
}

void Mapper183::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        low_prg_bank_ = addr & 15U;
        return;
    }
    if (addr >= 0xF000U) {
        switch ((addr >> 2U) & 3U) {
        case 0U:
            irq_latch_ = (irq_latch_ & 0xF0U) | (value & 15U);
            break;
        case 1U:
            irq_latch_ = (irq_latch_ & 15U) | ((value & 15U) << 4U);
            break;
        case 2U:
            irq_control_ = value & 7U;
            irq_enabled_ = (value >> 1U) & 1U;
            irq_pending_ = 0U;
            irq_prescaler_ = 341U;
            if (irq_enabled_ != 0U)
                irq_counter_ = irq_latch_;
            break;
        case 3U:
            irq_pending_ = 0U;
            irq_enabled_ = irq_control_ & 1U;
            break;
        default:
            break;
        }
        return;
    }
    vrc_.write_prg(addr, value);
}

bool Mapper183::maps_prg(Address addr) const {
    return (addr >= 0x6000U && addr < 0x8000U) || vrc_.maps_prg(addr);
}

Byte Mapper183::read_chr(Address addr) {
    return vrc_.read_chr(addr);
}

void Mapper183::write_chr(Address addr, Byte value) {
    vrc_.write_chr(addr, value);
}

MirrorMode Mapper183::mirror_mode() const {
    return vrc_.mirror_mode();
}

void Mapper183::reset() {
    vrc_.reset();
    low_prg_bank_ = 0U;
    irq_latch_ = irq_counter_ = irq_control_ = irq_enabled_ = irq_pending_ = 0U;
    irq_prescaler_ = 341U;
}

void Mapper183::power_on() {
    reset();
}

bool Mapper183::irq_pending() const {
    return irq_pending_ != 0U;
}

void Mapper183::acknowledge_irq() {
    irq_pending_ = 0U;
}

void Mapper183::clock_cpu() {
    if (irq_enabled_ == 0U)
        return;
    const auto tick = [this] {
        if (irq_counter_ == 255U) {
            irq_counter_ = irq_latch_;
            irq_pending_ = 1U;
        } else {
            ++irq_counter_;
        }
    };
    if ((irq_control_ & 4U) != 0U) {
        tick();
    } else if (irq_prescaler_ <= 3U) {
        irq_prescaler_ += 338U;
        tick();
    } else {
        irq_prescaler_ -= 3U;
    }
}

uint16_t Mapper183::state_tag() const {
    return static_cast<uint16_t>(0xB700U | (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper183::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write<uint32_t>(prg_size_);
    writer.write<uint32_t>(chr_size_);
    writer.write(low_prg_bank_);
    writer.write(irq_latch_);
    writer.write(irq_counter_);
    writer.write(irq_prescaler_);
    writer.write(irq_control_);
    writer.write(irq_enabled_);
    writer.write(irq_pending_);
    vrc_.save_state(writer);
}

void Mapper183::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_size = reader.read<uint32_t>();
    const auto chr_size = reader.read<uint32_t>();
    const auto low_prg_bank = reader.read<Byte>();
    const auto irq_latch = reader.read<Byte>();
    const auto irq_counter = reader.read<Byte>();
    const auto irq_prescaler = reader.read<uint16_t>();
    const auto irq_control = reader.read<Byte>();
    const auto irq_enabled = reader.read<Byte>();
    const auto irq_pending = reader.read<Byte>();
    uint64_t prg_ram_size = 0U;
    const auto vrc_state = read_vrc_state(reader, prg_ram_size);
    const bool valid_irq = irq_prescaler >= 1U && irq_prescaler <= 341U && irq_control <= 7U &&
                           irq_enabled <= 1U && irq_pending <= 1U &&
                           (irq_pending == 0U || irq_enabled != 0U);
    if (!reader.ok() || tag != state_tag() || prg_size != prg_size_ || chr_size != chr_size_ ||
        low_prg_bank > 15U || !valid_irq || !valid_vrc_state(vrc_state, prg_ram_size)) {
        reader.fail();
        return;
    }

    StateWriter staged_writer;
    write_vrc_state(staged_writer, vrc_state);
    const auto staged_bytes = staged_writer.take();
    StateReader staged_reader(staged_bytes);
    vrc_.load_state(staged_reader);
    if (!staged_reader.ok()) {
        reader.fail();
        return;
    }
    low_prg_bank_ = low_prg_bank;
    irq_latch_ = irq_latch;
    irq_counter_ = irq_counter;
    irq_prescaler_ = irq_prescaler;
    irq_control_ = irq_control;
    irq_enabled_ = irq_enabled;
    irq_pending_ = irq_pending;
}

} // namespace mapperbus::core
