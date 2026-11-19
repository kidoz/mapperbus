#include "core/mappers/mapper359.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinRomSize = 0x2000UZ;
constexpr std::size_t kMaxPrgSize = 0x100000UZ;
constexpr std::size_t kMaxChrSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::uint16_t kStateTag = 0x3590U;
constexpr std::array<unsigned, 4> kPrgMasks{0x3FU, 0x1FU, 0x2FU, 0x0FU};
constexpr Byte kIrqReload = 1U;
constexpr Byte kIrqPending = 2U;
constexpr Byte kIrqModeAuto = 4U;
constexpr Byte kIrqModePa12 = 2U;
constexpr Byte kIrqModeEnable = 1U;

} // namespace

// NESdev mapper 359 oldid 7945 defines the decoder, bank pins and IRQ sources.
// Size mode 2 uses the article's tentative $2F mask. Reset clears the registers
// and selects header H/V CIRAM; soft reset retains CHR RAM and cold power clears
// it. These are deterministic policies, not established electrical defaults.
Mapper359::Mapper359(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper359::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 359U) {
        return std::unexpected("Mapper 359 requires mapper ID 359");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 359 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinRomSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U) {
        return std::unexpected("Mapper 359 requires 8 KiB-1 MiB PRG ROM in 8 KiB units");
    }
    if (chr_size > kMaxChrSize || (chr_size != 0U && chr_size < kMinRomSize) ||
        chr_size % kChrBankSize != 0U) {
        return std::unexpected("Mapper 359 requires zero or 8-512 KiB CHR ROM in 1 KiB units");
    }
    const auto expected_chr_ram = chr_size == 0U ? kChrRamSize : 0UZ;
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != expected_chr_ram || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 359 requires no CPU RAM and only optional 8 KiB CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 359 requires ordinary H/V initial CIRAM metadata");
    }
    return {};
}

unsigned Mapper359::prg_inner_mask() const {
    return kPrgMasks[registers_.bank_size & 3U];
}

Byte Mapper359::read_prg(Address addr) {
    if (addr < 0x6000U) {
        return 0U;
    }
    const auto slot = static_cast<unsigned>((addr - 0x6000U) >> 13U);
    const unsigned inner = slot == 4U   ? prg_inner_mask()
                           : slot == 0U ? registers_.prg_banks[3]
                                        : registers_.prg_banks[slot - 1U];
    const unsigned outer = ((registers_.prg_base >> 3U) & 7U) * 16U;
    const unsigned bank = outer | (inner & prg_inner_mask());
    const auto offset = static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper359::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
    switch (addr & 0xF003U) {
    case 0x8000U:
    case 0x8001U:
    case 0x8002U:
    case 0x8003U:
        registers_.prg_banks[addr & 3U] = value;
        break;
    case 0x9000U:
        registers_.prg_base = value & 0x38U;
        break;
    case 0x9001U:
        registers_.bank_size = value & 0x43U;
        break;
    case 0x9002U:
        registers_.mirror = value & 3U;
        break;
    case 0x9003U:
        registers_.chr_base = value & 3U;
        break;
    case 0xA000U:
    case 0xA001U:
    case 0xA002U:
    case 0xA003U:
    case 0xB000U:
    case 0xB001U:
    case 0xB002U:
    case 0xB003U:
        registers_.chr_banks[((addr >> 10U) & 4U) | (addr & 3U)] = value;
        break;
    case 0xC000U:
        if ((registers_.irq_mode & kIrqModePa12) != 0U) {
            registers_.irq_flags |= kIrqReload;
            registers_.pa12_counter = 0U;
        } else {
            registers_.m2_counter =
                static_cast<std::uint16_t>((registers_.m2_counter & 0xFF00U) | value);
        }
        if ((registers_.irq_mode & kIrqModeAuto) != 0U) {
            set_irq_enabled(false);
        }
        break;
    case 0xC001U:
        if ((registers_.irq_mode & kIrqModePa12) != 0U) {
            registers_.irq_latch = value;
        } else {
            registers_.m2_counter = static_cast<std::uint16_t>(
                (registers_.m2_counter & 0x00FFU) | (static_cast<std::uint16_t>(value) << 8U));
        }
        if ((registers_.irq_mode & kIrqModeAuto) != 0U) {
            set_irq_enabled(true);
        }
        break;
    case 0xC002U:
        registers_.irq_mode = value & 7U;
        set_irq_enabled((registers_.irq_mode & kIrqModeEnable) != 0U);
        break;
    case 0xC003U:
        if ((registers_.irq_mode & kIrqModeAuto) == 0U) {
            set_irq_enabled((value & 1U) != 0U);
        }
        break;
    default:
        break;
    }
}

bool Mapper359::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper359::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper359::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return chr_ram_[addr & 0x1FFFU];
    }
    const auto slot = static_cast<unsigned>((addr >> 10U) & 7U);
    const unsigned mask = (registers_.bank_size & 0x40U) != 0U ? 0xFFU : 0x7FU;
    const unsigned outer = static_cast<unsigned>(registers_.chr_base) * 128U;
    const unsigned bank = outer | (registers_.chr_banks[slot] & mask);
    const auto offset = static_cast<std::size_t>(bank) * kChrBankSize + (addr & 0x03FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper359::write_chr(Address addr, Byte value) {
    if (chr_rom_.empty()) {
        chr_ram_[addr & 0x1FFFU] = value;
    }
}

MirrorMode Mapper359::mirror_mode() const {
    constexpr std::array<MirrorMode, 4> modes{MirrorMode::Vertical,
                                              MirrorMode::Horizontal,
                                              MirrorMode::SingleLower,
                                              MirrorMode::SingleUpper};
    return modes[registers_.mirror];
}

void Mapper359::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper359::reset() {
    registers_ = Registers{};
    registers_.mirror = initial_mirror_ == MirrorMode::Horizontal ? 1U : 0U;
}

bool Mapper359::irq_enabled() const {
    return (registers_.irq_mode & kIrqModeEnable) != 0U;
}

bool Mapper359::irq_pending() const {
    return (registers_.irq_flags & kIrqPending) != 0U;
}

void Mapper359::acknowledge_irq() {
    registers_.irq_flags &= ~kIrqPending;
}

void Mapper359::set_irq_enabled(bool enabled) {
    if (enabled) {
        registers_.irq_mode |= kIrqModeEnable;
    } else {
        registers_.irq_mode &= ~kIrqModeEnable;
        acknowledge_irq();
    }
}

void Mapper359::clock_pa12_irq() {
    if (registers_.pa12_counter == 0U || (registers_.irq_flags & kIrqReload) != 0U) {
        registers_.pa12_counter = registers_.irq_latch;
        registers_.irq_flags &= ~kIrqReload;
    } else {
        --registers_.pa12_counter;
    }
    if (registers_.pa12_counter == 0U && irq_enabled()) {
        registers_.irq_flags |= kIrqPending;
    }
}

void Mapper359::clock_cpu() {
    const bool a12_high = (registers_.a12_pins & 1U) != 0U;
    const auto low_cycles = static_cast<Byte>(registers_.a12_pins >> 1U);
    if (!a12_high && low_cycles < 3U) {
        registers_.a12_pins = static_cast<Byte>(registers_.a12_pins + 2U);
    }
    if ((registers_.irq_mode & kIrqModePa12) != 0U || !irq_enabled()) {
        return;
    }
    if (registers_.m2_counter != 0U) {
        --registers_.m2_counter;
    }
    if (registers_.m2_counter == 0U) {
        registers_.irq_flags |= kIrqPending;
    }
}

void Mapper359::observe_ppu_a12(bool high) {
    const bool was_high = (registers_.a12_pins & 1U) != 0U;
    const auto low_cycles = static_cast<Byte>(registers_.a12_pins >> 1U);
    if (high && !was_high && low_cycles >= 3U && (registers_.irq_mode & kIrqModePa12) != 0U) {
        clock_pa12_irq();
    }
    registers_.a12_pins = high ? 1U : 0U;
}

std::uint16_t Mapper359::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U) |
                                      (chr_rom_.empty() ? 0U : 2U));
}

void Mapper359::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write_array(registers_.prg_banks);
    writer.write(registers_.prg_base);
    writer.write(registers_.bank_size);
    writer.write(registers_.mirror);
    writer.write(registers_.chr_base);
    writer.write_array(registers_.chr_banks);
    writer.write(registers_.m2_counter);
    writer.write(registers_.irq_latch);
    writer.write(registers_.pa12_counter);
    writer.write(registers_.irq_mode);
    writer.write(registers_.irq_flags);
    writer.write(registers_.a12_pins);
    if (chr_rom_.empty()) {
        writer.write_array(chr_ram_);
    }
}

void Mapper359::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read_array(next.prg_banks);
    reader.read(next.prg_base);
    reader.read(next.bank_size);
    reader.read(next.mirror);
    reader.read(next.chr_base);
    reader.read_array(next.chr_banks);
    reader.read(next.m2_counter);
    reader.read(next.irq_latch);
    reader.read(next.pa12_counter);
    reader.read(next.irq_mode);
    reader.read(next.irq_flags);
    reader.read(next.a12_pins);
    std::array<Byte, kChrRamSize> chr_ram{};
    if (chr_rom_.empty()) {
        reader.read_array(chr_ram);
    }

    const bool valid_irq = next.irq_flags <= 3U && ((next.irq_flags & kIrqPending) == 0U ||
                                                    (next.irq_mode & kIrqModeEnable) != 0U);
    const bool valid_pins =
        next.a12_pins <= 6U && ((next.a12_pins & 1U) == 0U || next.a12_pins == 1U);
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || (next.prg_base & ~0x38U) != 0U ||
        (next.bank_size & ~0x43U) != 0U || next.mirror > 3U || next.chr_base > 3U ||
        next.irq_mode > 7U || !valid_irq || !valid_pins) {
        reader.fail();
        return;
    }

    registers_ = next;
    if (chr_rom_.empty()) {
        chr_ram_ = chr_ram;
    }
}

} // namespace mapperbus::core
