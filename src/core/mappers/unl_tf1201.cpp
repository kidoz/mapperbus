#include "core/mappers/unl_tf1201.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::uint16_t kStateMarker = 0xD298U;
constexpr std::size_t kPrgBankSize = 8192U;
constexpr std::size_t kChrBankSize = 1024U;

bool valid_runtime_mirror(MirrorMode mode) {
    return mode == MirrorMode::Horizontal || mode == MirrorMode::Vertical ||
           mode == MirrorMode::SingleLower || mode == MirrorMode::SingleUpper;
}

} // namespace

// NESdev mapper 298 r7772, VRC2/VRC4 r12375, VRC IRQ r12681 and VRC2 pinout
// r12399. Electrical reset is unspecified; reset() is a deterministic policy.
UnlTf1201::UnlTf1201(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_rom_(std::move(prg)), chr_rom_(std::move(chr)),
      has_prg_ram_(header.prg_ram_size == prg_ram_.size() ||
                   header.prg_nvram_size == prg_ram_.size()),
      has_prg_nvram_(header.prg_nvram_size == prg_ram_.size()), initial_mirror_(header.mirror_mode),
      mirror_(initial_mirror_) {
    reset();
}

Result<void> UnlTf1201::validate_geometry(const INesHeader& header,
                                          std::size_t prg_rom_size,
                                          std::size_t chr_rom_size) {
    if (header.mapper_number != 298U) {
        return std::unexpected("UNL-TF1201 requires mapper 298");
    }
    if (!header.is_nes2) {
        return std::unexpected("Mapper 298 requires a NES 2.0 header");
    }
    if (header.submapper != 0U) {
        return std::unexpected("Mapper 298 supports submapper 0");
    }
    if (prg_rom_size < 32768U || prg_rom_size > 262144U || !std::has_single_bit(prg_rom_size)) {
        return std::unexpected("Mapper 298 requires power-of-two 32-256 KiB PRG ROM");
    }
    if (chr_rom_size < 8192U || chr_rom_size > 524288U || !std::has_single_bit(chr_rom_size)) {
        return std::unexpected("Mapper 298 requires power-of-two 8-512 KiB CHR ROM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 298 requires H/V startup mirroring");
    }
    const bool has_ram = header.prg_ram_size != 0U;
    const bool has_nvram = header.prg_nvram_size != 0U;
    if ((has_ram && header.prg_ram_size != 2048U) ||
        (has_nvram && header.prg_nvram_size != 2048U) || (has_ram && has_nvram)) {
        return std::unexpected("Mapper 298 supports only optional 2 KiB PRG RAM or NVRAM");
    }
    if (header.has_battery != has_nvram) {
        return std::unexpected("Mapper 298 battery flag must match 2 KiB PRG NVRAM");
    }
    if (header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 298 does not expose CHR RAM or NVRAM");
    }
    return {};
}

Byte UnlTf1201::register_index(Address addr) {
    const auto value = static_cast<unsigned>(addr);
    return static_cast<Byte>(((value >> 1U) & 1U) | ((value & 1U) << 1U));
}

bool UnlTf1201::maps_prg(Address addr) const {
    if (addr >= 0x8000U) {
        return true;
    }
    return has_prg_ram_ && (mode_ & 1U) != 0U && addr >= 0x6000U && addr < 0x7000U;
}

std::size_t UnlTf1201::prg_bank_for_slot(unsigned slot) const {
    const std::size_t count = prg_rom_.size() / kPrgBankSize;
    if (slot == 1U) {
        return prg_banks_[1U];
    }
    if (slot == 3U) {
        return count - 1U;
    }
    const bool swapped = (mode_ & 2U) != 0U;
    const bool switchable = (slot == 0U) != swapped;
    return switchable ? prg_banks_[0U] : count - 2U;
}

Byte UnlTf1201::read_prg(Address addr) {
    if (addr < 0x8000U) {
        if (!maps_prg(addr)) {
            return 0U;
        }
        return prg_ram_[(addr - 0x6000U) & 0x07FFU];
    }
    if (prg_rom_.empty()) {
        return 0U;
    }
    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    const std::size_t count = prg_rom_.size() / kPrgBankSize;
    const std::size_t bank = prg_bank_for_slot(slot) % count;
    return prg_rom_[bank * kPrgBankSize + (addr & 0x1FFFU)];
}

void UnlTf1201::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        if (maps_prg(addr)) {
            prg_ram_[(addr - 0x6000U) & 0x07FFU] = value;
        }
        return;
    }

    const auto group = static_cast<unsigned>(addr) >> 12U;
    const Byte index = register_index(addr);
    if (group == 8U || group == 10U) {
        prg_banks_[group == 8U ? 0U : 1U] = value & 0x1FU;
        return;
    }
    if (group == 9U) {
        if (index == 0U) {
            constexpr std::array kMirrors = {MirrorMode::Vertical,
                                             MirrorMode::Horizontal,
                                             MirrorMode::SingleLower,
                                             MirrorMode::SingleUpper};
            mirror_ = kMirrors[value & 3U];
        } else if (index == 2U) {
            mode_ = value & 3U;
        }
        return;
    }
    if (group >= 11U && group <= 14U) {
        auto& bank = chr_banks_[(group - 11U) * 2U + (index >> 1U)];
        if ((index & 1U) == 0U) {
            bank = static_cast<std::uint16_t>((bank & 0x01F0U) | (value & 0x0FU));
        } else {
            bank = static_cast<std::uint16_t>((bank & 0x000FU) | ((value & 0x1FU) << 4U));
        }
        return;
    }
    if (group != 15U) {
        return;
    }
    switch (index) {
    case 0U:
        irq_latch_ = static_cast<Byte>((irq_latch_ & 0xF0U) | (value & 0x0FU));
        break;
    case 1U:
        irq_latch_ = static_cast<Byte>((irq_latch_ & 0x0FU) | ((value & 0x0FU) << 4U));
        break;
    case 2U:
        write_irq_control(value);
        break;
    case 3U:
        irq_pending_ = false;
        break;
    default:
        break;
    }
}

std::size_t UnlTf1201::chr_offset(Address addr) const {
    const std::size_t bank = chr_banks_[(addr >> 10U) & 7U];
    return bank * kChrBankSize + (addr & 0x03FFU);
}

Byte UnlTf1201::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    return chr_rom_[chr_offset(addr & 0x1FFFU) % chr_rom_.size()];
}

void UnlTf1201::write_chr(Address /*addr*/, Byte /*value*/) {}

MirrorMode UnlTf1201::mirror_mode() const {
    return mirror_;
}

void UnlTf1201::power_on() {
    reset();
    if (!has_prg_nvram_) {
        prg_ram_.fill(0U);
    }
}

void UnlTf1201::reset() {
    prg_banks_.fill(0U);
    chr_banks_.fill(0U);
    mirror_ = initial_mirror_;
    mode_ = 0U;
    irq_latch_ = 0U;
    irq_counter_ = 0U;
    irq_prescaler_ = 341U;
    irq_control_ = 0U;
    irq_pending_ = false;
}

void UnlTf1201::write_irq_control(Byte value) {
    irq_control_ = value & 7U;
    irq_pending_ = false;
    if ((irq_control_ & 2U) != 0U) {
        irq_counter_ = irq_latch_;
        irq_prescaler_ = 341U;
    }
}

void UnlTf1201::clock_irq() {
    if (irq_counter_ == 0xFFU) {
        irq_counter_ = irq_latch_;
        irq_pending_ = true;
    } else {
        ++irq_counter_;
    }
}

void UnlTf1201::clock_cpu() {
    if ((irq_control_ & 2U) == 0U) {
        return;
    }
    if ((irq_control_ & 4U) != 0U) {
        clock_irq();
    } else if (irq_prescaler_ <= 3U) {
        irq_prescaler_ = static_cast<std::uint16_t>(irq_prescaler_ + 338U);
        clock_irq();
    } else {
        irq_prescaler_ = static_cast<std::uint16_t>(irq_prescaler_ - 3U);
    }
}

bool UnlTf1201::irq_pending() const {
    return irq_pending_;
}

void UnlTf1201::acknowledge_irq() {
    irq_pending_ = false;
}

UnlTf1201::RamPopulation UnlTf1201::ram_population() const {
    if (has_prg_nvram_) {
        return RamPopulation::NonVolatile;
    }
    return has_prg_ram_ ? RamPopulation::Volatile : RamPopulation::None;
}

void UnlTf1201::save_state(StateWriter& writer) const {
    writer.write(kStateMarker);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(ram_population());
    writer.write(initial_mirror_);
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write(mirror_);
    writer.write(mode_);
    writer.write(irq_latch_);
    writer.write(irq_counter_);
    writer.write(irq_prescaler_);
    writer.write(irq_control_);
    writer.write(static_cast<Byte>(irq_pending_));
    if (has_prg_ram_) {
        writer.write_array(prg_ram_);
    }
}

void UnlTf1201::load_state(StateReader& reader) {
    const auto marker = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto population = reader.read<RamPopulation>();
    const auto initial_mirror = reader.read<MirrorMode>();
    std::array<Byte, 2> prg_banks{};
    std::array<std::uint16_t, 8> chr_banks{};
    reader.read_array(prg_banks);
    reader.read_array(chr_banks);
    const auto mirror = reader.read<MirrorMode>();
    const auto mode = reader.read<Byte>();
    const auto irq_latch = reader.read<Byte>();
    const auto irq_counter = reader.read<Byte>();
    const auto irq_prescaler = reader.read<std::uint16_t>();
    const auto irq_control = reader.read<Byte>();
    const auto irq_pending = reader.read<Byte>();
    std::array<Byte, 2048> prg_ram{};
    if (has_prg_ram_) {
        reader.read_array(prg_ram);
    }

    const bool valid =
        reader.ok() && marker == kStateMarker && prg_size == prg_rom_.size() &&
        chr_size == chr_rom_.size() && population == ram_population() &&
        initial_mirror == initial_mirror_ && valid_runtime_mirror(mirror) && prg_banks[0U] <= 31U &&
        prg_banks[1U] <= 31U &&
        std::ranges::all_of(chr_banks, [](std::uint16_t bank) { return bank <= 511U; }) &&
        mode <= 3U && irq_prescaler >= 1U && irq_prescaler <= 341U && irq_control <= 7U &&
        irq_pending <= 1U && (irq_pending == 0U || (irq_control & 2U) != 0U);
    if (!valid) {
        reader.fail();
        return;
    }

    prg_banks_ = prg_banks;
    chr_banks_ = chr_banks;
    mirror_ = mirror;
    mode_ = mode;
    irq_latch_ = irq_latch;
    irq_counter_ = irq_counter;
    irq_prescaler_ = irq_prescaler;
    irq_control_ = irq_control;
    irq_pending_ = irq_pending != 0U;
    if (has_prg_ram_) {
        prg_ram_ = prg_ram;
    }
}

std::span<const Byte> UnlTf1201::battery_ram() const {
    if (!has_prg_nvram_) {
        return {};
    }
    return prg_ram_;
}

void UnlTf1201::set_battery_ram(std::span<const Byte> data) {
    if (!has_prg_nvram_ || data.size() != prg_ram_.size()) {
        return;
    }
    std::ranges::copy(data, prg_ram_.begin());
}

} // namespace mapperbus::core
