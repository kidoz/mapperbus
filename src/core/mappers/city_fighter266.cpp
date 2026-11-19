#include "core/mappers/city_fighter266.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

CityFighter266::CityFighter266(const INesHeader& header,
                               std::vector<Byte> prg_rom,
                               std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), mirror_(initial_mirror_) {}

Result<void> CityFighter266::validate_geometry(const INesHeader& header,
                                               std::size_t prg_size,
                                               std::size_t chr_size) {
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 266 requires NES 2.0 submapper 0");
    }
    if (prg_size < 32768U || prg_size > 131072U || !std::has_single_bit(prg_size) ||
        chr_size < 8192U || chr_size > 524288U || !std::has_single_bit(chr_size)) {
        return std::unexpected(
            "Mapper 266 requires power-of-two 32-128 KiB PRG and 8-512 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 266 supports ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 266 requires H/V startup mirroring");
    }
    return {};
}

Byte CityFighter266::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    const auto offset = static_cast<std::size_t>(prg_bank_) * 32768U + (addr & 0x7FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void CityFighter266::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
    if ((addr & 0xF00CU) == 0x9000U) {
        constexpr std::array kModes = {MirrorMode::Vertical,
                                       MirrorMode::Horizontal,
                                       MirrorMode::SingleLower,
                                       MirrorMode::SingleUpper};
        mirror_ = kModes[value & 3U];
        return;
    }
    if ((addr & 0xF80CU) == 0x900CU) {
        prg_bank_ = static_cast<Byte>((static_cast<unsigned>(value) >> 2U) & 3U);
        return;
    }
    if ((addr & 0xF80CU) == 0x980CU) {
        dac_ = value & 15U;
        return;
    }

    const auto index = (static_cast<unsigned>(addr) >> 2U) & 3U;
    unsigned chr_base = 0U;
    switch (addr & 0xF000U) {
    case 0xA000U:
        chr_base = 2U;
        break;
    case 0xB000U:
        chr_base = 4U;
        break;
    case 0xD000U:
        chr_base = 0U;
        break;
    case 0xE000U:
        chr_base = 6U;
        break;
    case 0xF000U:
        switch (index) {
        case 0U:
            irq_latch_ = (irq_latch_ & 0xF0U) | (value & 15U);
            break;
        case 1U:
            irq_latch_ = (irq_latch_ & 15U) | ((value & 15U) << 4U);
            break;
        case 2U:
            write_irq_control(value);
            break;
        case 3U:
            write_irq_acknowledge();
            break;
        default:
            break;
        }
        return;
    default:
        return;
    }

    auto& bank = chr_banks_[chr_base + (index >> 1U)];
    if ((index & 1U) == 0U) {
        bank = (bank & 0x1F0U) | (value & 15U);
    } else {
        bank = (bank & 15U) | ((value & 31U) << 4U);
    }
}

Byte CityFighter266::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    const auto slot = static_cast<unsigned>(addr >> 10U) & 7U;
    const auto offset = static_cast<std::size_t>(chr_banks_[slot]) * 1024U + (addr & 0x03FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void CityFighter266::write_chr(Address, Byte) {}

MirrorMode CityFighter266::mirror_mode() const {
    return mirror_;
}

void CityFighter266::reset() {
    chr_banks_.fill(0U);
    mirror_ = initial_mirror_;
    prg_bank_ = 0U;
    dac_ = 0U;
    irq_latch_ = 0U;
    irq_counter_ = 0U;
    irq_prescaler_ = 341U;
    irq_control_ = 0U;
    irq_enabled_ = 0U;
    irq_pending_ = 0U;
}

void CityFighter266::write_irq_control(Byte value) {
    irq_control_ = value & 7U;
    irq_enabled_ = static_cast<Byte>((static_cast<unsigned>(value) >> 1U) & 1U);
    irq_pending_ = 0U;
    irq_prescaler_ = 341U;
    if (irq_enabled_ != 0U) {
        irq_counter_ = irq_latch_;
    }
}

void CityFighter266::write_irq_acknowledge() {
    irq_pending_ = 0U;
    irq_enabled_ = irq_control_ & 1U;
}

void CityFighter266::clock_irq() {
    if (irq_counter_ == 0xFFU) {
        irq_counter_ = irq_latch_;
        irq_pending_ = 1U;
    } else {
        ++irq_counter_;
    }
}

void CityFighter266::clock_cpu() {
    if (irq_enabled_ == 0U) {
        return;
    }
    if ((irq_control_ & 4U) != 0U) {
        clock_irq();
    } else if (irq_prescaler_ <= 3U) {
        irq_prescaler_ += 338U;
        clock_irq();
    } else {
        irq_prescaler_ -= 3U;
    }
}

bool CityFighter266::irq_pending() const {
    return irq_pending_ != 0U;
}

void CityFighter266::acknowledge_irq() {
    irq_pending_ = 0U;
}

bool CityFighter266::has_expansion_audio() const {
    return true;
}

float CityFighter266::audio_output() const {
    return static_cast<float>(dac_) / 15.0F;
}

uint16_t CityFighter266::state_tag() const {
    const auto prg = std::bit_width(prg_rom_.size() / 32768U) - 1U;
    const auto chr = std::bit_width(chr_rom_.size() / 8192U) - 1U;
    return static_cast<uint16_t>(0xA600U | (prg << 4U) | (chr << 1U) |
                                 (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void CityFighter266::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(prg_bank_);
    writer.write_array(chr_banks_);
    writer.write(mirror_);
    writer.write(dac_);
    writer.write(irq_latch_);
    writer.write(irq_counter_);
    writer.write(irq_prescaler_);
    writer.write(irq_control_);
    writer.write(irq_enabled_);
    writer.write(irq_pending_);
}

void CityFighter266::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_bank = reader.read<Byte>();
    std::array<uint16_t, 8> chr_banks{};
    reader.read_array(chr_banks);
    const auto mirror = reader.read<MirrorMode>();
    const auto dac = reader.read<Byte>();
    const auto irq_latch = reader.read<Byte>();
    const auto irq_counter = reader.read<Byte>();
    const auto irq_prescaler = reader.read<uint16_t>();
    const auto irq_control = reader.read<Byte>();
    const auto irq_enabled = reader.read<Byte>();
    const auto irq_pending = reader.read<Byte>();
    const bool valid_chr =
        std::ranges::all_of(chr_banks, [](uint16_t bank) { return bank <= 511U; });
    if (!reader.ok() || tag != state_tag() || prg_bank > 3U || !valid_chr ||
        mirror == MirrorMode::FourScreen || mirror > MirrorMode::SingleUpper || dac > 15U ||
        irq_prescaler == 0U || irq_prescaler > 341U || irq_control > 7U || irq_enabled > 1U ||
        irq_pending > 1U || (irq_pending != 0U && irq_enabled == 0U)) {
        reader.fail();
        return;
    }
    prg_bank_ = prg_bank;
    chr_banks_ = chr_banks;
    mirror_ = mirror;
    dac_ = dac;
    irq_latch_ = irq_latch;
    irq_counter_ = irq_counter;
    irq_prescaler_ = irq_prescaler;
    irq_control_ = irq_control;
    irq_enabled_ = irq_enabled;
    irq_pending_ = irq_pending;
}

} // namespace mapperbus::core
