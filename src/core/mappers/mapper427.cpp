#include "core/mappers/mapper427.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x0008000UZ;
constexpr std::size_t kMaxPrgSize = 0x2000000UZ;
constexpr std::size_t kMinChrSize = 0x0002000UZ;
constexpr std::size_t kMaxChrSize = 0x2000000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::uint16_t kStateTag = 0x4270U;
constexpr std::array<unsigned, 8> kPrgMasks{63U, 31U, 15U, 7U, 3U, 1U, 0U, 255U};
constexpr std::array<unsigned, 8> kChrMasks{255U, 127U, 63U, 0U, 31U, 15U, 7U, 0U};

bool in_gpio(Address addr, Address base) {
    return addr >= base && addr < base + 8U;
}

} // namespace

// Original implementation from NES 2.0 mapper 427 oldid 23382. GPIO wiring and
// serial protocols follow the published NintendulatorNRS behavioral reference at
// b685625. Reset clears registers and releases serial pins but retains EEPROM;
// these are deterministic policies, not established electrical reset behavior.
Mapper427::Mapper427(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), submapper_(header.submapper),
      has_eeprom_(header.prg_nvram_size == kEepromSize) {
    eeprom_data_.fill(0xFFU);
    reset();
}

Result<void> Mapper427::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 427U) {
        return std::unexpected("Mapper 427 requires mapper ID 427");
    }
    if (!header.is_nes2 || header.submapper > 1U) {
        return std::unexpected("Mapper 427 requires NES 2.0 submapper 0 or 1");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        (chr_size != 0U &&
         (!std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxChrSize))) {
        return std::unexpected("Mapper 427 requires power-of-two 32 KiB-32 MiB PRG and optional 8 "
                               "KiB-32 MiB CHR ROM");
    }
    if (header.prg_ram_size != 0U ||
        (header.prg_nvram_size != 0U && header.prg_nvram_size != kEepromSize) ||
        header.has_battery != (header.prg_nvram_size == kEepromSize) || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 427 supports only optional 512-byte battery-backed 24C04");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 427 requires ordinary H/V CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 427 does not provide trainer RAM");
    }
    return {};
}

bool Mapper427::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper427::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

std::size_t Mapper427::prg_address(Address addr) const {
    auto slot = static_cast<unsigned>((addr - 0x8000U) / kPrgBankSize);
    if ((registers_.control & 0x40U) != 0U && (slot & 1U) == 0U) {
        slot ^= 2U;
    }
    const unsigned inner = slot < 2U                                       ? registers_.prg[slot]
                           : slot == 2U && (registers_.mode & 0x40U) != 0U ? registers_.prg[2]
                           : slot == 2U                                    ? 254U
                                                                           : 255U;
    const unsigned mask = kPrgMasks[registers_.mode & 7U];
    const unsigned bank =
        (inner & mask) | (registers_.middle & (255U ^ mask)) | ((registers_.outer >> 4U) << 8U);
    return static_cast<std::size_t>(bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

Byte Mapper427::read_prg(Address addr) {
    return maps_prg(addr) ? prg_rom_[prg_address(addr) & (prg_rom_.size() - 1U)] : 0U;
}

void Mapper427::write_prg(Address addr, Byte value) {
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

std::size_t Mapper427::chr_address(Address addr) const {
    auto slot = static_cast<unsigned>((addr & (kChrBankSize * 8U - 1U)) / kChrBankSize);
    if ((registers_.control & 0x80U) != 0U) {
        slot ^= 4U;
    }
    const unsigned inner = slot < 4U ? (registers_.chr[4U + slot / 2U] & 254U) | (slot & 1U)
                                     : registers_.chr[slot - 4U];
    const unsigned mask = kChrMasks[registers_.chr_middle & 7U];
    const unsigned bank = (inner & mask) | (registers_.chr_middle & (255U ^ mask) & 248U) |
                          ((registers_.intermediate >> 4U) << 8U) |
                          ((registers_.outer & 15U) << 11U);
    return static_cast<std::size_t>(bank) * kChrBankSize + (addr & (kChrBankSize - 1U));
}

Byte Mapper427::read_chr(Address addr) {
    const auto& storage = chr_rom_.empty() ? prg_rom_ : chr_rom_;
    return storage[chr_address(addr) & (storage.size() - 1U)];
}

void Mapper427::write_chr(Address, Byte) {}

MirrorMode Mapper427::mirror_mode() const {
    return registers_.mirror != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

std::optional<Address> Mapper427::ciram_address(Address addr) const {
    return addr < 0x2000U && (registers_.control & 0x20U) != 0U
               ? std::optional<Address>(addr & 0x07FFU)
               : std::nullopt;
}

bool Mapper427::maps_expansion(Address addr) const {
    const Address protection_base = submapper_ == 0U ? 0x4148U : 0x4150U;
    return (addr >= 0x4100U && addr <= 0x410BU) || in_gpio(addr, protection_base) ||
           (has_eeprom_ && in_gpio(addr, 0x4150U));
}

Byte Mapper427::expansion_read_mask(Address addr) const {
    if (!maps_expansion(addr)) {
        return 0xFFU;
    }
    const bool gpio1 = submapper_ == 0U && in_gpio(addr, 0x4148U);
    const bool gpio2 = in_gpio(addr, 0x4150U) && (submapper_ == 1U || has_eeprom_);
    return (gpio1 || gpio2) && (addr == 0x4150U || addr == 0x4152U || addr == 0x4153U ||
                                addr == 0x4148U || addr == 0x414AU || addr == 0x414BU)
               ? 0xFFU
               : 0U;
}

Byte Mapper427::read_expansion(Address addr) {
    if (submapper_ == 0U && in_gpio(addr, 0x4148U)) {
        return read_gpio(gpio1_, 1U, addr);
    }
    if (in_gpio(addr, 0x4150U) && (submapper_ == 1U || has_eeprom_)) {
        return read_gpio(gpio2_, 2U, addr);
    }
    return 0U;
}

void Mapper427::write_expansion(Address addr, Byte value) {
    if (addr >= 0x4100U && addr <= 0x410BU) {
        write_system(addr & 15U, value);
    } else if (submapper_ == 0U && in_gpio(addr, 0x4148U)) {
        write_gpio(gpio1_, 1U, addr, value);
    } else if (in_gpio(addr, 0x4150U) && (submapper_ == 1U || has_eeprom_)) {
        write_gpio(gpio2_, 2U, addr, value);
    }
}

Byte Mapper427::read_gpio(GpioRegisters& gpio, unsigned port, Address addr) {
    switch (addr & 7U) {
    case 0U:
        return gpio.mask;
    case 2U:
        return gpio.latch;
    case 3U:
        gpio.state = static_cast<Byte>(~gpio.mask);
        if ((port == 1U && submapper_ == 0U) || (port == 2U && submapper_ == 1U)) {
            const auto bit = static_cast<Byte>(1U << (submapper_ == 0U ? 4U : 0U));
            gpio.state &=
                static_cast<Byte>(protection_.output ? bit : 0U) | static_cast<Byte>(~bit);
        }
        if (port == 2U && has_eeprom_) {
            gpio.state &= static_cast<Byte>(eeprom_.output ? 0x08U : 0U) | 0xF7U;
        }
        return gpio.state;
    default:
        return 0U;
    }
}

void Mapper427::write_gpio(GpioRegisters& gpio, unsigned port, Address addr, Byte value) {
    switch (addr & 7U) {
    case 0U:
        gpio.mask = value;
        break;
    case 2U:
    case 3U:
        gpio.latch = value;
        break;
    default:
        return;
    }
    gpio.state =
        static_cast<Byte>((gpio.state & static_cast<Byte>(~gpio.mask)) | (gpio.latch & gpio.mask));
    if ((port == 1U && submapper_ == 0U) || (port == 2U && submapper_ == 1U)) {
        const unsigned shift = port == 1U ? 4U : 0U;
        set_protection_pins((gpio.state & (1U << (shift + 1U))) != 0U,
                            (gpio.state & (1U << shift)) != 0U);
    }
    if (port == 2U && has_eeprom_) {
        set_eeprom_pins((gpio.state & 0x04U) != 0U, (gpio.state & 0x08U) != 0U);
    }
}

void Mapper427::set_protection_pins(bool clock, bool data) {
    if (protection_.clock && clock && protection_.data && !data) {
        protection_.stage = 1U;
    } else if (protection_.clock && clock && !protection_.data && data) {
        protection_.stage = 19U;
    } else if (!protection_.clock && clock) {
        if (protection_.stage == 0U) {
            protection_.data = data;
        } else if (protection_.stage < 9U) {
            protection_.command = static_cast<Byte>(
                (static_cast<unsigned>(protection_.command) << 1U) | (data ? 1U : 0U));
            ++protection_.stage;
            if (protection_.stage == 9U && protection_.command != 0x80U) {
                protection_.stage = 0U;
            }
        } else if (protection_.stage == 9U) {
            protection_.stage = 10U;
        } else if (protection_.stage < 18U) {
            protection_.result = static_cast<Byte>(
                (static_cast<unsigned>(protection_.result) << 1U) | (data ? 1U : 0U));
            ++protection_.stage;
            if (protection_.stage == 18U) {
                const auto transformed = static_cast<Byte>(0U - protection_.result);
                protection_.result = static_cast<Byte>((transformed >> 4U) | (transformed << 4U));
            }
        } else if (protection_.stage == 18U) {
            protection_.stage = 0U;
        } else {
            protection_.output = (protection_.result & 0x80U) != 0U;
            protection_.result <<= 1U;
        }
    }
    protection_.clock = clock;
    protection_.data = data;
}

void Mapper427::set_eeprom_pins(bool clock, bool data) {
    if (eeprom_.clock && clock && eeprom_.data && !data) {
        eeprom_.stage = 1U;
    } else if (eeprom_.clock && clock && !eeprom_.data && data) {
        eeprom_.stage = 0U;
    } else if (eeprom_.clock && !clock) {
        receive_eeprom_bit();
    }
    eeprom_.clock = clock;
    eeprom_.data = data;
}

void Mapper427::receive_eeprom_bit() {
    switch (eeprom_.stage) {
    case 1U:
        eeprom_.stage = 2U;
        break;
    case 2U:
    case 3U:
    case 4U:
    case 5U: {
        const bool expected = (0x0AU & (1U << (5U - eeprom_.stage))) != 0U;
        eeprom_.stage = expected == eeprom_.data ? eeprom_.stage + 1U : 0U;
        break;
    }
    case 6U:
    case 7U:
        eeprom_.stage = eeprom_.data ? 0U : eeprom_.stage + 1U;
        break;
    case 8U:
        eeprom_.address =
            static_cast<Word>((eeprom_.address & 0x00FFU) | (eeprom_.data ? 0x0100U : 0U));
        ++eeprom_.stage;
        break;
    case 9U:
        eeprom_.read_mode = eeprom_.data;
        ++eeprom_.stage;
        break;
    case 10U:
        eeprom_.bit = 0U;
        if (eeprom_.read_mode) {
            eeprom_.latch = eeprom_data_[eeprom_.address];
            eeprom_.stage = 11U;
        } else {
            eeprom_.latch = 0U;
            eeprom_.stage = 12U;
        }
        break;
    case 11U:
        if (++eeprom_.bit == 8U) {
            eeprom_.address =
                static_cast<Word>((eeprom_.address & 0x0100U) | ((eeprom_.address + 1U) & 0x00FFU));
            eeprom_.stage = 10U;
        }
        break;
    case 12U:
        eeprom_.latch |= static_cast<Byte>(eeprom_.data ? 0x80U >> eeprom_.bit : 0U);
        if (++eeprom_.bit == 8U) {
            eeprom_.address = static_cast<Word>((eeprom_.address & 0x0100U) | eeprom_.latch);
            eeprom_.stage = 13U;
        }
        break;
    case 13U:
        eeprom_.bit = 0U;
        eeprom_.latch = 0U;
        eeprom_.stage = 14U;
        break;
    case 14U:
        eeprom_.latch |= static_cast<Byte>(eeprom_.data ? 0x80U >> eeprom_.bit : 0U);
        if (++eeprom_.bit == 8U) {
            eeprom_data_[eeprom_.address] = eeprom_.latch;
            eeprom_.address =
                static_cast<Word>((eeprom_.address & 0x0100U) | ((eeprom_.address + 1U) & 0x00FFU));
            eeprom_.stage = 13U;
        }
        break;
    default:
        break;
    }

    switch (eeprom_.stage) {
    case 10U:
    case 13U:
        eeprom_.output = false;
        break;
    case 11U:
        eeprom_.output = (eeprom_.latch & (0x80U >> eeprom_.bit)) != 0U;
        break;
    default:
        eeprom_.output = true;
        break;
    }
}

void Mapper427::write_system(unsigned reg, Byte value) {
    switch (reg) {
    case 0U:
        registers_.outer = value;
        break;
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

std::optional<Byte> Mapper427::read_ppu_register(Address addr, Byte open_bus) {
    return addr >= 0x2010U && addr <= 0x201FU ? std::optional<Byte>(open_bus) : std::nullopt;
}

bool Mapper427::write_ppu_register(Address addr, Byte value) {
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

void Mapper427::reset() {
    registers_ = Registers{};
    gpio1_ = GpioRegisters{};
    gpio2_ = GpioRegisters{};
    protection_ = ProtectionRegisters{};
    eeprom_ = EepromRegisters{};
}

bool Mapper427::irq_pending() const {
    return registers_.pending;
}

bool Mapper427::observes_ppu_a12() const {
    return true;
}

void Mapper427::clock_counter() {
    registers_.counter = registers_.counter == 0U ? registers_.latch : registers_.counter - 1U;
    if (registers_.counter == 0U && registers_.enabled) {
        registers_.pending = true;
    }
}

void Mapper427::clock_cpu() {
    if (!registers_.a12 && registers_.low_cycles < 3U) {
        ++registers_.low_cycles;
    }
}

void Mapper427::observe_ppu_a12(bool high) {
    if (high && !registers_.a12 && registers_.low_cycles >= 3U && (registers_.mode & 0x80U) == 0U) {
        clock_counter();
    }
    registers_.a12 = high;
    if (high) {
        registers_.low_cycles = 0U;
    }
}

void Mapper427::on_ppu_hblank() {
    if ((registers_.mode & 0x80U) != 0U) {
        clock_counter();
    }
}

std::span<const Byte> Mapper427::battery_ram() const {
    return has_eeprom_ ? std::span<const Byte>{eeprom_data_} : std::span<const Byte>{};
}

void Mapper427::set_battery_ram(std::span<const Byte> data) {
    if (has_eeprom_ && data.size() == eeprom_data_.size()) {
        std::ranges::copy(data, eeprom_data_.begin());
    }
}

void Mapper427::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write<std::uint32_t>(has_eeprom_ ? kEepromSize : 0U);
    writer.write(submapper_);
    writer.write(registers_.outer);
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
    writer.write(gpio1_.mask);
    writer.write(gpio1_.latch);
    writer.write(gpio1_.state);
    writer.write(gpio2_.mask);
    writer.write(gpio2_.latch);
    writer.write(gpio2_.state);
    writer.write(protection_.stage);
    writer.write(protection_.command);
    writer.write(protection_.result);
    writer.write<Byte>(protection_.clock ? 1U : 0U);
    writer.write<Byte>(protection_.data ? 1U : 0U);
    writer.write<Byte>(protection_.output ? 1U : 0U);
    writer.write(eeprom_.stage);
    writer.write<Byte>(eeprom_.clock ? 1U : 0U);
    writer.write<Byte>(eeprom_.data ? 1U : 0U);
    writer.write<Byte>(eeprom_.output ? 1U : 0U);
    writer.write<Byte>(eeprom_.read_mode ? 1U : 0U);
    writer.write(eeprom_.address);
    writer.write(eeprom_.bit);
    writer.write(eeprom_.latch);
    if (has_eeprom_) {
        writer.write_array(eeprom_data_);
    }
}

void Mapper427::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto eeprom_size = reader.read<std::uint32_t>();
    const auto submapper = reader.read<Byte>();
    Registers next;
    reader.read(next.outer);
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
    GpioRegisters gpio1;
    GpioRegisters gpio2;
    reader.read(gpio1.mask);
    reader.read(gpio1.latch);
    reader.read(gpio1.state);
    reader.read(gpio2.mask);
    reader.read(gpio2.latch);
    reader.read(gpio2.state);
    ProtectionRegisters protection;
    reader.read(protection.stage);
    reader.read(protection.command);
    reader.read(protection.result);
    const auto protection_clock = reader.read<Byte>();
    const auto protection_data = reader.read<Byte>();
    const auto protection_output = reader.read<Byte>();
    EepromRegisters eeprom;
    reader.read(eeprom.stage);
    const auto eeprom_clock = reader.read<Byte>();
    const auto eeprom_data = reader.read<Byte>();
    const auto eeprom_output = reader.read<Byte>();
    const auto eeprom_read_mode = reader.read<Byte>();
    reader.read(eeprom.address);
    reader.read(eeprom.bit);
    reader.read(eeprom.latch);
    std::array<Byte, kEepromSize> data{};
    if (has_eeprom_) {
        reader.read_array(data);
    }

    const bool valid_protection = protection.stage <= 19U && protection_clock <= 1U &&
                                  protection_data <= 1U && protection_output <= 1U &&
                                  (protection.stage < 9U || protection.command == 0x80U);
    const bool valid_eeprom = eeprom.stage <= 14U && eeprom_clock <= 1U && eeprom_data <= 1U &&
                              eeprom_output <= 1U && eeprom_read_mode <= 1U &&
                              eeprom.address < kEepromSize && eeprom.bit <= 8U &&
                              (eeprom.stage != 11U || eeprom.bit < 8U) &&
                              ((eeprom.stage != 12U && eeprom.stage != 14U) || eeprom.bit < 8U);
    const bool valid = reader.ok() && tag == kStateTag && prg_size == prg_rom_.size() &&
                       chr_size == chr_rom_.size() &&
                       eeprom_size == (has_eeprom_ ? kEepromSize : 0U) && submapper == submapper_ &&
                       (next.control & ~0xE7U) == 0U && next.mirror <= 1U &&
                       (next.mode & ~0xCFU) == 0U && (next.intermediate & ~0x70U) == 0U &&
                       irq <= 3U && irq != 2U && pins <= 6U && ((pins & 1U) == 0U || pins == 1U) &&
                       valid_protection && valid_eeprom;
    if (!valid) {
        reader.fail();
        return;
    }

    next.enabled = (irq & 1U) != 0U;
    next.pending = (irq & 2U) != 0U;
    next.a12 = (pins & 1U) != 0U;
    next.low_cycles = pins >> 1U;
    protection.clock = protection_clock != 0U;
    protection.data = protection_data != 0U;
    protection.output = protection_output != 0U;
    eeprom.clock = eeprom_clock != 0U;
    eeprom.data = eeprom_data != 0U;
    eeprom.output = eeprom_output != 0U;
    eeprom.read_mode = eeprom_read_mode != 0U;
    registers_ = next;
    gpio1_ = gpio1;
    gpio2_ = gpio2;
    protection_ = protection;
    eeprom_ = eeprom;
    if (has_eeprom_) {
        eeprom_data_ = data;
    }
}

} // namespace mapperbus::core
