#include "core/mappers/mapper308.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankBytes = 8192U;
constexpr std::size_t kChrBankBytes = 1024U;
constexpr std::size_t kPrgRomBytes = 128U * 1024U;
constexpr std::size_t kChrRomBytes = 128U * 1024U;
constexpr uint16_t kIrqLowCounterMask = 0x0FFFU;
constexpr uint16_t kIrqLowCounterMidpoint = 0x0800U;

} // namespace

// Original implementation from NESdev mapper 308 r7793, VRC2/VRC4, and the
// TH2131-1 PCB discussion. No usable PRG RAM or Microwire interface is populated.
Mapper308::Mapper308(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), mirror_(initial_mirror_) {
    power_on();
}

Result<void> Mapper308::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 308 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgRomBytes || chr_size != kChrRomBytes) {
        return std::unexpected("Mapper 308 requires 128 KiB PRG and 128 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 308 supports ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 308 requires H/V startup mirroring");
    }
    return {};
}

Byte Mapper308::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }

    const auto slot = static_cast<unsigned>((addr - 0x8000U) / kPrgBankBytes);
    const std::size_t bank_count = prg_rom_.size() / kPrgBankBytes;
    const std::size_t bank = slot < 2U ? prg_banks_[slot] : bank_count - 2U + slot - 2U;
    const std::size_t offset = (bank % bank_count) * kPrgBankBytes + (addr & (kPrgBankBytes - 1U));
    return prg_rom_[offset];
}

void Mapper308::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }

    const auto group = static_cast<unsigned>(addr >> 12U);
    const auto port = static_cast<unsigned>(addr & 3U);
    if (group == 8U || group == 10U) {
        prg_banks_[group == 8U ? 0U : 1U] = value & 0x1FU;
    } else if (group == 9U) {
        mirror_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    } else if (group >= 11U && group <= 14U) {
        auto& bank = chr_banks_[(group - 11U) * 2U + port / 2U];
        if ((port & 1U) == 0U) {
            bank = static_cast<Byte>((bank & 0xF0U) | (value & 0x0FU));
        } else {
            bank = static_cast<Byte>((bank & 0x0FU) | ((value & 0x0FU) << 4U));
        }
    }

    switch (addr & 0xF003U) {
    case 0xF000U:
        irq_enabled_ = false;
        irq_low_counter_ = 0U;
        break;
    case 0xF001U:
        irq_enabled_ = true;
        break;
    case 0xF003U:
        irq_high_counter_ = value >> 4U;
        break;
    default:
        break;
    }
}

Byte Mapper308::read_chr(Address addr) {
    const auto slot = static_cast<unsigned>((addr >> 10U) & 7U);
    const std::size_t offset =
        static_cast<std::size_t>(chr_banks_[slot]) * kChrBankBytes + (addr & 0x03FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper308::write_chr(Address, Byte) {}

MirrorMode Mapper308::mirror_mode() const {
    return mirror_;
}

void Mapper308::power_on() {
    prg_banks_ = {0U, 1U};
    chr_banks_.fill(0U);
    mirror_ = initial_mirror_;
    irq_low_counter_ = 0U;
    irq_high_counter_ = 0U;
    irq_enabled_ = false;
}

void Mapper308::reset() {
    // No console-reset input is documented for the clone or its IRQ glue.
}

bool Mapper308::irq_pending() const {
    return irq_enabled_ && irq_high_counter_ == 0U && irq_low_counter_ < kIrqLowCounterMidpoint;
}

void Mapper308::clock_cpu() {
    if (!irq_enabled_) {
        return;
    }
    irq_low_counter_ = (irq_low_counter_ + 1U) & kIrqLowCounterMask;
    if (irq_low_counter_ == kIrqLowCounterMidpoint) {
        irq_high_counter_ = (irq_high_counter_ - 1U) & 0x0FU;
    }
}

uint16_t Mapper308::state_tag() const {
    return static_cast<uint16_t>(0x3080U | (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper308::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<uint32_t>(prg_rom_.size()));
    writer.write(static_cast<uint32_t>(chr_rom_.size()));
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write(static_cast<Byte>(mirror_));
    writer.write(irq_low_counter_);
    writer.write(irq_high_counter_);
    writer.write<Byte>(irq_enabled_ ? 1U : 0U);
}

void Mapper308::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_size = reader.read<uint32_t>();
    const auto chr_size = reader.read<uint32_t>();
    std::array<Byte, 2> prg_banks{};
    std::array<Byte, 8> chr_banks{};
    reader.read_array(prg_banks);
    reader.read_array(chr_banks);
    const auto mirror = reader.read<Byte>();
    const auto low_counter = reader.read<uint16_t>();
    const auto high_counter = reader.read<Byte>();
    const auto enabled = reader.read<Byte>();

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || prg_banks[0] > 0x1FU || prg_banks[1] > 0x1FU ||
        mirror > static_cast<Byte>(MirrorMode::Vertical) || low_counter > kIrqLowCounterMask ||
        high_counter > 0x0FU || enabled > 1U || (enabled == 0U && low_counter != 0U)) {
        reader.fail();
        return;
    }

    prg_banks_ = prg_banks;
    chr_banks_ = chr_banks;
    mirror_ = static_cast<MirrorMode>(mirror);
    irq_low_counter_ = low_counter;
    irq_high_counter_ = high_counter;
    irq_enabled_ = enabled != 0U;
}

} // namespace mapperbus::core
