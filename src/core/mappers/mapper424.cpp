#include "core/mappers/mapper424.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x0008000UZ;
constexpr std::size_t kMaxPrgSize = 0x4000000UZ;
constexpr std::size_t kMinChrSize = 0x0002000UZ;
constexpr std::size_t kMaxChrSize = 0x4000000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::uint16_t kStateTag = 0x4240U;
constexpr std::array<unsigned, 8> kPrgMasks{63U, 31U, 15U, 7U, 3U, 1U, 0U, 255U};
constexpr std::array<unsigned, 8> kChrMasks{255U, 127U, 63U, 0U, 31U, 15U, 7U, 0U};

} // namespace

// Original implementation from NES 2.0 mapper 424 oldid 23379. The $411E
// register's low bit adds A25 to the normal 32 MiB OneBus PRG/CHR decoding.
// Cleared registers and vertical CIRAM are deterministic reset compatibility
// policies; the pinned board description does not establish electrical reset.
Mapper424::Mapper424(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> Mapper424::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 424U) {
        return std::unexpected("Mapper 424 requires mapper ID 424");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 424 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        (chr_size != 0U &&
         (!std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxChrSize))) {
        return std::unexpected(
            "Mapper 424 requires power-of-two 32 KiB-64 MiB PRG and optional 8 KiB-64 MiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 424 provides no cartridge RAM or NVRAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 424 requires ordinary H/V CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 424 does not provide trainer RAM");
    }
    return {};
}

bool Mapper424::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper424::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

std::size_t Mapper424::prg_address(Address addr) const {
    auto slot = static_cast<unsigned>((addr - 0x8000U) / kPrgBankSize);
    if ((registers_.control & 0x40U) != 0U && (slot & 1U) == 0U) {
        slot ^= 2U;
    }
    const unsigned inner = slot < 2U                                       ? registers_.prg[slot]
                           : slot == 2U && (registers_.mode & 0x40U) != 0U ? registers_.prg[2]
                           : slot == 2U                                    ? 254U
                                                                           : 255U;
    const unsigned mask = kPrgMasks[registers_.mode & 7U];
    const unsigned bank = (inner & mask) | (registers_.middle & (255U ^ mask)) |
                          ((registers_.outer >> 4U) << 8U) |
                          (static_cast<unsigned>(registers_.extension) << 12U);
    return static_cast<std::size_t>(bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

Byte Mapper424::read_prg(Address addr) {
    return maps_prg(addr) ? prg_rom_[prg_address(addr) & (prg_rom_.size() - 1U)] : 0U;
}

void Mapper424::write_prg(Address addr, Byte value) {
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

std::size_t Mapper424::chr_address(Address addr) const {
    auto slot = static_cast<unsigned>((addr & (kChrBankSize * 8U - 1U)) / kChrBankSize);
    if ((registers_.control & 0x80U) != 0U) {
        slot ^= 4U;
    }
    const unsigned inner = slot < 4U ? (registers_.chr[4U + slot / 2U] & 254U) | (slot & 1U)
                                     : registers_.chr[slot - 4U];
    const unsigned mask = kChrMasks[registers_.chr_middle & 7U];
    const unsigned bank = (inner & mask) | (registers_.chr_middle & (255U ^ mask) & 248U) |
                          ((registers_.intermediate >> 4U) << 8U) |
                          ((registers_.outer & 15U) << 11U) |
                          (static_cast<unsigned>(registers_.extension) << 15U);
    return static_cast<std::size_t>(bank) * kChrBankSize + (addr & (kChrBankSize - 1U));
}

Byte Mapper424::read_chr(Address addr) {
    const auto& storage = chr_rom_.empty() ? prg_rom_ : chr_rom_;
    return storage[chr_address(addr) & (storage.size() - 1U)];
}

void Mapper424::write_chr(Address, Byte) {}

MirrorMode Mapper424::mirror_mode() const {
    return registers_.mirror != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

std::optional<Address> Mapper424::ciram_address(Address addr) const {
    return addr < 0x2000U && (registers_.control & 0x20U) != 0U
               ? std::optional<Address>(addr & 0x07FFU)
               : std::nullopt;
}

bool Mapper424::maps_expansion(Address addr) const {
    return (addr >= 0x4100U && addr <= 0x410BU) || addr == 0x411EU;
}

Byte Mapper424::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0U : 0xFFU;
}

void Mapper424::write_expansion(Address addr, Byte value) {
    if (addr >= 0x4100U && addr <= 0x410BU) {
        write_system(addr & 15U, value);
    } else if (addr == 0x411EU) {
        registers_.extension = value & 1U;
    }
}

void Mapper424::write_system(unsigned reg, Byte value) {
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

std::optional<Byte> Mapper424::read_ppu_register(Address addr, Byte open_bus) {
    return addr >= 0x2010U && addr <= 0x201FU ? std::optional<Byte>(open_bus) : std::nullopt;
}

bool Mapper424::write_ppu_register(Address addr, Byte value) {
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

void Mapper424::reset() {
    registers_ = Registers{};
}

bool Mapper424::irq_pending() const {
    return registers_.pending;
}

bool Mapper424::observes_ppu_a12() const {
    return true;
}

void Mapper424::clock_counter() {
    registers_.counter = registers_.counter == 0U ? registers_.latch : registers_.counter - 1U;
    if (registers_.counter == 0U && registers_.enabled) {
        registers_.pending = true;
    }
}

void Mapper424::clock_cpu() {
    if (!registers_.a12 && registers_.low_cycles < 3U) {
        ++registers_.low_cycles;
    }
}

void Mapper424::observe_ppu_a12(bool high) {
    if (high && !registers_.a12 && registers_.low_cycles >= 3U && (registers_.mode & 0x80U) == 0U) {
        clock_counter();
    }
    registers_.a12 = high;
    if (high) {
        registers_.low_cycles = 0U;
    }
}

void Mapper424::on_ppu_hblank() {
    if ((registers_.mode & 0x80U) != 0U) {
        clock_counter();
    }
}

void Mapper424::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write<std::uint32_t>(0U);
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
    writer.write(registers_.extension);
}

void Mapper424::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto ram_size = reader.read<std::uint32_t>();
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
    const auto extension = reader.read<Byte>();

    const bool valid = reader.ok() && tag == kStateTag && prg_size == prg_rom_.size() &&
                       chr_size == chr_rom_.size() && ram_size == 0U &&
                       (next.control & ~0xE7U) == 0U && next.mirror <= 1U &&
                       (next.mode & ~0xCFU) == 0U && (next.intermediate & ~0x70U) == 0U &&
                       irq <= 3U && irq != 2U && pins <= 6U && ((pins & 1U) == 0U || pins == 1U) &&
                       extension <= 1U;
    if (!valid) {
        reader.fail();
        return;
    }

    next.enabled = (irq & 1U) != 0U;
    next.pending = (irq & 2U) != 0U;
    next.a12 = (pins & 1U) != 0U;
    next.low_cycles = pins >> 1U;
    next.extension = extension;
    registers_ = next;
}

} // namespace mapperbus::core
