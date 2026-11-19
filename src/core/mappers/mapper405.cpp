#include "core/mappers/mapper405.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x100000UZ;
constexpr std::size_t kChrRamSize = 0x008000UZ;
constexpr std::size_t kEepromSize = 256UZ;
constexpr std::array<unsigned, 8> kPrgMasks{63U, 31U, 15U, 7U, 3U, 1U, 0U, 255U};
constexpr std::array<unsigned, 8> kChrMasks{255U, 127U, 63U, 0U, 31U, 15U, 7U, 0U};
constexpr std::uint16_t kStateTag = 0x4050U;

} // namespace

// Original implementation from NESdev mapper 405 oldid 23367 and the existing
// native OneBus register model. Soft reset restores deterministic native register
// defaults while retaining RAM; cold power also clears volatile CHR RAM.
Mapper405::Mapper405(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), has_eeprom_(header.prg_nvram_size == kEepromSize) {
    chr_rom.clear();
    eeprom_.fill(0xFFU);
    power_on();
}

Result<void> Mapper405::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 405U) {
        return std::unexpected("Mapper 405 requires mapper ID 405");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 405 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        chr_size != 0U) {
        return std::unexpected(
            "Mapper 405 requires 32 KiB-1 MiB power-of-two PRG ROM and no CHR ROM");
    }
    if (header.prg_ram_size != 0U ||
        (header.prg_nvram_size != 0U && header.prg_nvram_size != kEepromSize) ||
        header.has_battery != (header.prg_nvram_size == kEepromSize) ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 405 requires 32 KiB CHR RAM and optional 256-byte PRG NVRAM only");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 405 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 405 does not provide trainer RAM");
    }
    return {};
}

bool Mapper405::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper405::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

std::size_t Mapper405::prg_address(Address addr) const {
    auto slot = static_cast<unsigned>((addr - 0x8000U) / 8192U);
    if ((registers_.control & 0x40U) != 0U && (slot & 1U) == 0U) {
        slot ^= 2U;
    }
    const unsigned inner = slot < 2U                                       ? registers_.prg[slot]
                           : slot == 2U && (registers_.mode & 0x40U) != 0U ? registers_.prg[2]
                           : slot == 2U                                    ? 254U
                                                                           : 255U;
    const unsigned mask = kPrgMasks[registers_.mode & 7U];
    const unsigned bank = (inner & mask) | (registers_.middle & (255U ^ mask));
    return static_cast<std::size_t>(bank) * 8192U + (addr & 8191U);
}

Byte Mapper405::read_prg(Address addr) {
    if (!maps_prg(addr) || prg_rom_.empty()) {
        return 0U;
    }
    return prg_rom_[prg_address(addr) & (prg_rom_.size() - 1U)];
}

void Mapper405::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U || (registers_.mode & 8U) != 0U) {
        return;
    }
    switch (addr & 0xE001U) {
    case 0x8000U:
        write_system(5U, static_cast<Byte>((value & 0xDFU) | (registers_.control & 0x20U)));
        break;
    case 0x8001U: {
        const unsigned index = registers_.control & 7U;
        if (index < 6U) {
            registers_.chr[index < 2U ? index + 4U : index - 2U] = value;
        } else {
            registers_.prg[index - 6U] = value;
        }
        break;
    }
    case 0xA000U:
        write_system(6U, value);
        break;
    case 0xC000U:
        write_system(1U, value);
        break;
    case 0xC001U:
        write_system(2U, value);
        break;
    case 0xE000U:
        write_system(3U, value);
        break;
    case 0xE001U:
        write_system(4U, value);
        break;
    default:
        break;
    }
}

std::size_t Mapper405::chr_address(Address addr) const {
    auto slot = static_cast<unsigned>((addr & 8191U) / 1024U);
    if ((registers_.control & 0x80U) != 0U) {
        slot ^= 4U;
    }
    const unsigned inner = slot < 4U ? (registers_.chr[4U + slot / 2U] & 254U) | (slot & 1U)
                                     : registers_.chr[slot - 4U];
    const unsigned mask = kChrMasks[registers_.chr_middle & 7U];
    const unsigned bank = (inner & mask) | (registers_.chr_middle & (255U ^ mask) & 248U) |
                          ((registers_.intermediate >> 4U) << 8U);
    return static_cast<std::size_t>(bank) * 1024U + (addr & 1023U);
}

Byte Mapper405::read_chr(Address addr) {
    return chr_ram_[chr_address(addr) & (chr_ram_.size() - 1U)];
}

void Mapper405::write_chr(Address addr, Byte value) {
    chr_ram_[chr_address(addr) & (chr_ram_.size() - 1U)] = value;
}

MirrorMode Mapper405::mirror_mode() const {
    return registers_.mirror != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

std::optional<Address> Mapper405::ciram_address(Address addr) const {
    return addr < 0x2000U && (registers_.control & 0x20U) != 0U
               ? std::optional<Address>(addr & 0x7FFU)
               : std::nullopt;
}

bool Mapper405::maps_expansion(Address addr) const {
    return addr >= 0x4101U && addr <= 0x410BU;
}

Byte Mapper405::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0U : 0xFFU;
}

void Mapper405::write_expansion(Address addr, Byte value) {
    if (maps_expansion(addr)) {
        write_system(addr & 15U, value);
    }
}

void Mapper405::write_system(unsigned reg, Byte value) {
    switch (reg) {
    case 1U:
        registers_.latch = value;
        break;
    case 2U:
        registers_.counter = 0U;
        break;
    case 3U:
        registers_.enabled = registers_.pending = false;
        break;
    case 4U:
        registers_.enabled = true;
        break;
    case 5U:
        registers_.control = value & 0xE7U;
        break;
    case 6U:
        registers_.mirror = value & 1U;
        break;
    case 7U:
    case 8U:
    case 9U:
        registers_.prg[reg - 7U] = value;
        break;
    case 10U:
        registers_.middle = value;
        break;
    case 11U:
        registers_.mode = value & 0xCFU;
        break;
    default:
        break;
    }
}

std::optional<Byte> Mapper405::read_ppu_register(Address addr, Byte open_bus) {
    return addr >= 0x2010U && addr <= 0x201FU ? std::optional<Byte>(open_bus) : std::nullopt;
}

bool Mapper405::write_ppu_register(Address addr, Byte value) {
    if (addr < 0x2010U || addr > 0x201FU) {
        return false;
    }
    if (addr >= 0x2012U && addr <= 0x2017U) {
        registers_.chr[addr - 0x2012U] = value;
    } else if (addr == 0x2018U) {
        registers_.intermediate = value & 0x70U;
    } else if (addr == 0x201AU) {
        registers_.chr_middle = value;
    }
    return true;
}

void Mapper405::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper405::reset() {
    registers_ = Registers{};
}

bool Mapper405::irq_pending() const {
    return registers_.pending;
}

bool Mapper405::observes_ppu_a12() const {
    return true;
}

void Mapper405::clock_counter() {
    registers_.counter = registers_.counter == 0U ? registers_.latch : registers_.counter - 1U;
    if (registers_.counter == 0U && registers_.enabled) {
        registers_.pending = true;
    }
}

void Mapper405::clock_cpu() {
    if (!registers_.a12 && registers_.low_cycles < 3U) {
        ++registers_.low_cycles;
    }
}

void Mapper405::observe_ppu_a12(bool high) {
    if (high && !registers_.a12 && registers_.low_cycles >= 3U && (registers_.mode & 0x80U) == 0U) {
        clock_counter();
    }
    registers_.a12 = high;
    if (high) {
        registers_.low_cycles = 0U;
    }
}

void Mapper405::on_ppu_hblank() {
    if ((registers_.mode & 0x80U) != 0U) {
        clock_counter();
    }
}

std::span<const Byte> Mapper405::battery_ram() const {
    return has_eeprom_ ? std::span<const Byte>{eeprom_} : std::span<const Byte>{};
}

void Mapper405::set_battery_ram(std::span<const Byte> data) {
    if (has_eeprom_ && data.size() == eeprom_.size()) {
        std::ranges::copy(data, eeprom_.begin());
    }
}

void Mapper405::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write<std::uint32_t>(has_eeprom_ ? kEepromSize : 0U);
    writer.write(registers_.control);
    writer.write(registers_.mirror);
    writer.write_array(registers_.prg);
    writer.write(registers_.middle);
    writer.write(registers_.mode);
    writer.write_array(registers_.chr);
    writer.write(registers_.intermediate);
    writer.write(registers_.chr_middle);
    writer.write(registers_.latch);
    writer.write(registers_.counter);
    writer.write<Byte>((registers_.enabled ? 1U : 0U) | (registers_.pending ? 2U : 0U));
    writer.write<Byte>((registers_.a12 ? 1U : 0U) | (registers_.low_cycles << 1U));
    writer.write_array(chr_ram_);
    if (has_eeprom_) {
        writer.write_array(eeprom_);
    }
}

void Mapper405::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto nvram_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.control);
    reader.read(next.mirror);
    reader.read_array(next.prg);
    reader.read(next.middle);
    reader.read(next.mode);
    reader.read_array(next.chr);
    reader.read(next.intermediate);
    reader.read(next.chr_middle);
    reader.read(next.latch);
    reader.read(next.counter);
    const auto irq = reader.read<Byte>();
    const auto pins = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);
    std::array<Byte, kEepromSize> eeprom{};
    if (has_eeprom_) {
        reader.read_array(eeprom);
    }

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() || chr_size != 0U ||
        nvram_size != (has_eeprom_ ? kEepromSize : 0U) || (next.control & ~0xE7U) != 0U ||
        next.mirror > 1U || (next.mode & ~0xCFU) != 0U || (next.intermediate & ~0x70U) != 0U ||
        irq > 3U || irq == 2U || pins > 6U || ((pins & 1U) != 0U && pins != 1U)) {
        reader.fail();
        return;
    }

    next.enabled = (irq & 1U) != 0U;
    next.pending = (irq & 2U) != 0U;
    next.a12 = (pins & 1U) != 0U;
    next.low_cycles = pins >> 1U;
    registers_ = next;
    chr_ram_ = chr_ram;
    if (has_eeprom_) {
        eeprom_ = eeprom;
    }
}

} // namespace mapperbus::core
