#include "core/mappers/mapper273.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankBytes = 8192U;
constexpr std::size_t kChrBankBytes = 1024U;
constexpr std::size_t kMinPrgBytes = 32768U;
constexpr std::size_t kMaxPrgBytes = 32U * kPrgBankBytes;
constexpr std::size_t kMaxChrBytes = 256U * kChrBankBytes;

} // namespace

// Original implementation from NESdev r7703 and its linked PCB trace. CPU A2/A3
// connect to VRC A0/A1; the separate IRQ is clocked at each M2 falling edge.
Mapper273::Mapper273(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), mirror_(initial_mirror_) {
    power_on();
}

Result<void> Mapper273::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U) {
        return std::unexpected("Mapper 273 supports submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgBytes || prg_size > kMaxPrgBytes ||
        !std::has_single_bit(chr_size) || chr_size < 8192U || chr_size > kMaxChrBytes) {
        return std::unexpected(
            "Mapper 273 requires power-of-two 32-256 KiB PRG and 8-256 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_nvram_size != 0U || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U || (header.is_nes2 && header.prg_ram_size != 0U)) {
        return std::unexpected("Mapper 273 supports ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 273 requires H/V startup mirroring");
    }
    return {};
}

Byte Mapper273::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    const auto slot = static_cast<unsigned>((addr - 0x8000U) / kPrgBankBytes);
    const std::size_t last_bank = prg_rom_.size() / kPrgBankBytes - 1U;
    const std::size_t bank = slot < 2U ? prg_banks_[slot] : last_bank - (slot == 2U ? 1U : 0U);
    const auto offset = bank * kPrgBankBytes + (addr & (kPrgBankBytes - 1U));
    return prg_rom_[offset & (prg_rom_.size() - 1U)];
}

void Mapper273::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }

    const auto group = static_cast<unsigned>(addr >> 12U);
    const auto port = static_cast<unsigned>((addr >> 2U) & 3U);
    if (group == 8U) {
        prg_banks_[0] = value & 0x1FU;
    } else if (group == 10U) {
        prg_banks_[1] = value & 0x1FU;
    } else if (group == 9U) {
        mirror_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    } else if (group >= 11U && group <= 14U) {
        const auto slot = (group - 11U) * 2U + port / 2U;
        const auto low = static_cast<Byte>(value & 0x0FU);
        if ((port & 1U) == 0U) {
            chr_banks_[slot] = static_cast<Byte>((chr_banks_[slot] & 0xF0U) | low);
        } else {
            chr_banks_[slot] = static_cast<Byte>((chr_banks_[slot] & 0x0FU) | (low << 4U));
        }
    }

    switch (addr & 0xF00CU) {
    case 0xF000U:
        irq_counter_ = value;
        load_suppresses_count_ = true;
        break;
    case 0xF008U:
        irq_enabled_ = (value & 1U) != 0U;
        if (!irq_enabled_) {
            irq_prescaler_ = 0U;
        }
        break;
    default:
        break;
    }
}

Byte Mapper273::read_chr(Address addr) {
    const auto slot = static_cast<unsigned>((addr >> 10U) & 7U);
    const auto offset =
        static_cast<std::size_t>(chr_banks_[slot]) * kChrBankBytes + (addr & (kChrBankBytes - 1U));
    return chr_rom_[offset & (chr_rom_.size() - 1U)];
}

void Mapper273::write_chr(Address, Byte) {}

MirrorMode Mapper273::mirror_mode() const {
    return mirror_;
}

void Mapper273::power_on() {
    // The trace does not establish electrical power-up values. These are
    // deterministic cold-start values; the fixed upper PRG window is physical.
    prg_banks_ = {0U, 1U};
    chr_banks_.fill(0U);
    mirror_ = initial_mirror_;
    irq_counter_ = 0U;
    irq_prescaler_ = 0U;
    irq_enabled_ = false;
    load_suppresses_count_ = false;
}

void Mapper273::reset() {
    // No console-reset input is documented for either the VRC clone or IRQ glue.
}

bool Mapper273::irq_pending() const {
    return irq_enabled_ && irq_counter_ == 0xFFU && irq_prescaler_ < 0x80U;
}

void Mapper273::clock_cpu_falling_edge() {
    if (irq_enabled_) {
        irq_prescaler_ = static_cast<Byte>(irq_prescaler_ + 1U);
        if (irq_prescaler_ == 0x80U && !load_suppresses_count_) {
            irq_counter_ = static_cast<Byte>(irq_counter_ + 1U);
        }
    }
    load_suppresses_count_ = false;
}

uint16_t Mapper273::state_tag() const {
    return static_cast<uint16_t>(0x2730U | (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper273::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<uint32_t>(prg_rom_.size()));
    writer.write(static_cast<uint32_t>(chr_rom_.size()));
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write(static_cast<Byte>(mirror_));
    writer.write(irq_counter_);
    writer.write(irq_prescaler_);
    writer.write<Byte>(irq_enabled_ ? 1U : 0U);
    writer.write<Byte>(load_suppresses_count_ ? 1U : 0U);
}

void Mapper273::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_size = reader.read<uint32_t>();
    const auto chr_size = reader.read<uint32_t>();
    std::array<Byte, 2> prg_banks{};
    std::array<Byte, 8> chr_banks{};
    reader.read_array(prg_banks);
    reader.read_array(chr_banks);
    const auto mirror = reader.read<Byte>();
    const auto counter = reader.read<Byte>();
    const auto prescaler = reader.read<Byte>();
    const auto enabled = reader.read<Byte>();
    const auto load_pending = reader.read<Byte>();

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || prg_banks[0] > 31U || prg_banks[1] > 31U ||
        mirror > static_cast<Byte>(MirrorMode::Vertical) || enabled > 1U || load_pending > 1U ||
        (enabled == 0U && prescaler != 0U)) {
        reader.fail();
        return;
    }

    prg_banks_ = prg_banks;
    chr_banks_ = chr_banks;
    mirror_ = static_cast<MirrorMode>(mirror);
    irq_counter_ = counter;
    irq_prescaler_ = prescaler;
    irq_enabled_ = enabled != 0U;
    load_suppresses_count_ = load_pending != 0U;
}

} // namespace mapperbus::core
