#include "core/mappers/mapper400.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x400000UZ;
constexpr std::size_t kOuterPrgBankSize = 0x020000UZ;
constexpr std::size_t kInnerPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrBankSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x4000U;
constexpr Byte kLcdMemoryWrite = 0x2CU;

} // namespace

// Original implementation from NESdev mapper 400 oldid 22764. Electrical reset is
// unspecified: deterministic register reset and RAM retention policies are used.
Mapper400::Mapper400(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), initial_mirror_(header.mirror_mode) {
    chr_rom.clear();
    power_on();
}

Result<void> Mapper400::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 400U) {
        return std::unexpected("Mapper 400 requires mapper ID 400");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 400 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != 0U) {
        return std::unexpected("Mapper 400 requires exactly 4 MiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0x8000U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 400 requires only 32 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 400 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 400 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper400::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    const bool upper = addr >= 0xC000U;
    const auto outer = upper ? registers_.upper_outer_bank : registers_.lower_outer_bank;
    const auto inner = upper ? 7U : registers_.inner_prg_bank;
    const auto offset = static_cast<std::size_t>(outer) * kOuterPrgBankSize +
                        static_cast<std::size_t>(inner) * kInnerPrgBankSize + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper400::write_prg(Address addr, Byte value) {
    if ((addr & 0xF83FU) == 0x7000U) {
        registers_.lower_outer_bank = (value >> 3U) & 0x1FU;
    }
    if ((addr & 0xF800U) == 0x7800U) {
        registers_.upper_outer_bank = (value >> 3U) & 0x1FU;
        registers_.mirror = (value & 0x10U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    }
    if ((addr & 0xF800U) == 0x6800U) {
        registers_.lcd_control = value & 3U;
    }
    if (addr >= 0x8000U && addr < 0xC000U) {
        registers_.leds = value;
    }
    if (addr >= 0xC000U) {
        registers_.inner_prg_bank = value & 7U;
        registers_.chr_bank = (value >> 4U) & 3U;
    }
}

void Mapper400::observe_cpu_write(Address addr, Byte value) {
    if ((addr & 0xF800U) != 0x6800U) {
        write_lcd_data(value);
    }
}

void Mapper400::write_lcd_data(Byte value) {
    if ((registers_.lcd_control & 1U) != 0U) {
        return;
    }
    if ((registers_.lcd_control & 2U) == 0U) {
        registers_.lcd_command = value;
        registers_.lcd_data_count = 0U;
        return;
    }
    if (registers_.lcd_command == kLcdMemoryWrite && registers_.lcd_data_count < lcd_data_.size()) {
        lcd_data_[registers_.lcd_data_count] = value;
        ++registers_.lcd_data_count;
    }
}

bool Mapper400::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper400::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper400::read_chr(Address addr) {
    return chr_ram_[static_cast<std::size_t>(registers_.chr_bank) * kChrBankSize +
                    (addr & 0x1FFFU)];
}

void Mapper400::write_chr(Address addr, Byte value) {
    chr_ram_[static_cast<std::size_t>(registers_.chr_bank) * kChrBankSize + (addr & 0x1FFFU)] =
        value;
}

MirrorMode Mapper400::mirror_mode() const {
    return registers_.mirror;
}

std::optional<Byte> Mapper400::led_outputs() const {
    return static_cast<Byte>(~registers_.leds);
}

void Mapper400::power_on() {
    reset();
    chr_ram_.fill(0U);
    lcd_data_.fill(0U);
}

void Mapper400::reset() {
    registers_ = Registers{};
    registers_.mirror = initial_mirror_;
}

std::uint16_t Mapper400::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper400::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(registers_.inner_prg_bank);
    writer.write(registers_.lower_outer_bank);
    writer.write(registers_.upper_outer_bank);
    writer.write(registers_.chr_bank);
    writer.write(static_cast<Byte>(registers_.mirror));
    writer.write(registers_.leds);
    writer.write(registers_.lcd_control);
    writer.write(registers_.lcd_command);
    writer.write(registers_.lcd_data_count);
    writer.write_array(chr_ram_);
    writer.write_array(lcd_data_);
}

void Mapper400::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.inner_prg_bank);
    reader.read(next.lower_outer_bank);
    reader.read(next.upper_outer_bank);
    reader.read(next.chr_bank);
    const auto mirror = reader.read<Byte>();
    reader.read(next.leds);
    reader.read(next.lcd_control);
    reader.read(next.lcd_command);
    reader.read(next.lcd_data_count);
    std::array<Byte, 0x8000> chr_ram{};
    std::array<Byte, kLcdDataSize> lcd_data{};
    reader.read_array(chr_ram);
    reader.read_array(lcd_data);

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() || chr_size != 0U ||
        next.inner_prg_bank > 7U || next.lower_outer_bank > 0x1FU ||
        next.upper_outer_bank > 0x1FU || next.chr_bank > 3U || mirror > 1U ||
        next.lcd_control > 3U || next.lcd_data_count > kLcdDataSize ||
        (next.lcd_command != kLcdMemoryWrite && next.lcd_data_count != 0U)) {
        reader.fail();
        return;
    }

    next.mirror = mirror != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    registers_ = next;
    chr_ram_ = chr_ram;
    lcd_data_ = lcd_data;
}

} // namespace mapperbus::core
