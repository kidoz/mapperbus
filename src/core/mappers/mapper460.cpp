#include "core/mappers/mapper460.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrRomSize = 0x080000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000800UZ;
constexpr std::uint16_t kStateTag = 0x4600U;
constexpr Address kExtraMask = 0x0167U;
constexpr Address kPrgDisable = 0x0100U;
constexpr Address kNromMode = 0x0040U;
constexpr Address kNrom256Mode = 0x0020U;
constexpr Address kChrRomSelect = 0x0004U;
constexpr Byte kIrqReload = 1U;
constexpr Byte kIrqEnabled = 2U;
constexpr Byte kIrqPending = 4U;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 460 oldid 21050. Reset clears
// the documented extra latch and deterministic MMC3 registers, while retaining CHR RAM.
Mapper460::Mapper460(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper460::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 460U) {
        return std::unexpected("Mapper 460 requires mapper ID 460");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 460 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize) {
        return std::unexpected("Mapper 460 requires power-of-two 32-512 KiB PRG ROM");
    }
    if (chr_size != kChrRomSize || header.chr_ram_size != kChrRamSize) {
        return std::unexpected("Mapper 460 requires 512 KiB CHR ROM and 8 KiB CHR RAM");
    }
    if (header.prg_ram_size != 0U || header.prg_nvram_size != 0U || header.chr_nvram_size != 0U ||
        header.has_battery) {
        return std::unexpected("Mapper 460 has no CPU RAM or nonvolatile storage");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 460 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 460 does not provide trainer RAM");
    }
    return {};
}

bool Mapper460::nrom_mode() const {
    return (registers_.extra & kNromMode) != 0U;
}

bool Mapper460::nrom256_mode() const {
    return (registers_.extra & (kNromMode | kNrom256Mode)) == (kNromMode | kNrom256Mode);
}

bool Mapper460::prg_rom_disabled() const {
    return solder_pad_connected_ && (registers_.extra & kPrgDisable) != 0U;
}

bool Mapper460::chr_ram_selected() const {
    return (registers_.extra & kChrRomSelect) == 0U;
}

Byte Mapper460::inner_prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    if (nrom_mode()) {
        Byte bank = registers_.banks[(slot & 1U) == 0U ? 6U : 7U];
        if (nrom256_mode()) {
            bank = static_cast<Byte>((bank & ~2U) | (slot & 2U));
        }
        return bank;
    }
    const bool swapped = (registers_.bank_mode & 0x40U) != 0U;
    return slot == 1U                ? registers_.banks[7U]
           : slot == 3U              ? 63U
           : (slot == 0U) == swapped ? 62U
                                     : registers_.banks[6U];
}

std::size_t Mapper460::prg_offset(Address addr) const {
    const unsigned inner = inner_prg_bank(addr) & 0x1FU;
    const unsigned outer = registers_.extra & 3U;

    return static_cast<std::size_t>(inner | (outer << 5U)) * kPrgBankSize +
           (addr & (kPrgBankSize - 1U));
}

Byte Mapper460::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_disabled() || prg_rom_.empty()) {
        return 0U;
    }
    return prg_rom_[prg_offset(addr) % prg_rom_.size()];
}

void Mapper460::write_prg(Address addr, Byte value) {
    if ((addr & 0xE000U) == 0x6000U) {
        if ((registers_.ram_flags & 0x80U) != 0U) {
            registers_.extra = addr & kExtraMask;
        }
        return;
    }
    if (addr < 0x8000U) {
        return;
    }

    switch (addr & 0xE001U) {
    case 0x8000U:
        registers_.bank_select = value & 7U;
        registers_.bank_mode = value & 0xC0U;
        break;
    case 0x8001U:
        registers_.banks[registers_.bank_select] =
            registers_.bank_select >= 6U ? static_cast<Byte>(value & 0x3FU) : value;
        break;
    case 0xA000U:
        registers_.mirror = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
        registers_.ram_flags = value & 0xC0U;
        break;
    case 0xC000U:
        registers_.irq_reload = value;
        break;
    case 0xC001U:
        registers_.irq_counter = 0U;
        registers_.irq_flags |= kIrqReload;
        break;
    case 0xE000U:
        registers_.irq_flags &= kIrqReload;
        break;
    case 0xE001U:
        registers_.irq_flags |= kIrqEnabled;
        break;
    default:
        break;
    }
}

bool Mapper460::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper460::prg_read_mask(Address addr) const {
    return maps_prg(addr) && !prg_rom_disabled() ? 0xFFU : 0U;
}

Byte Mapper460::inner_chr_bank(Address addr) const {
    const unsigned slot = ((addr >> 10U) & 7U) ^ ((registers_.bank_mode & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return static_cast<Byte>((registers_.banks[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return registers_.banks[slot - 2U];
}

std::size_t Mapper460::chr_offset(Address addr) const {
    const auto pins =
        static_cast<Address>((addr & 0x1000U) | ((addr & 0x800U) >> 1U) | (addr & 0x800U));
    return static_cast<std::size_t>(inner_chr_bank(pins)) * kChrBankSize + (addr & 0x07FFU);
}

Byte Mapper460::read_chr(Address addr) {
    if (chr_ram_selected()) {
        return chr_ram_[addr & (kChrRamSize - 1U)];
    }
    if (chr_rom_.empty()) {
        return 0U;
    }
    return chr_rom_[chr_offset(addr) % chr_rom_.size()];
}

void Mapper460::write_chr(Address addr, Byte value) {
    if (chr_ram_selected()) {
        chr_ram_[addr & (kChrRamSize - 1U)] = value;
    }
}

MirrorMode Mapper460::mirror_mode() const {
    return registers_.mirror;
}

Result<void> Mapper460::configure_board_jumpers(Byte value) {
    if (value > 1U) {
        return std::unexpected("Mapper 460 solder pad accepts 0 (open) or 1 (connected)");
    }
    solder_pad_connected_ = value != 0U;
    return {};
}

void Mapper460::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper460::reset() {
    registers_ = {};
    registers_.mirror = initial_mirror_;
}

bool Mapper460::irq_pending() const {
    return (registers_.irq_flags & kIrqPending) != 0U;
}

void Mapper460::acknowledge_irq() {
    registers_.irq_flags &= static_cast<Byte>(~kIrqPending);
}

void Mapper460::clock_irq_counter() {
    if (registers_.irq_counter == 0U || (registers_.irq_flags & kIrqReload) != 0U) {
        registers_.irq_counter = registers_.irq_reload;
        registers_.irq_flags &= static_cast<Byte>(~kIrqReload);
    } else {
        --registers_.irq_counter;
    }
    if (registers_.irq_counter == 0U && (registers_.irq_flags & kIrqEnabled) != 0U) {
        registers_.irq_flags |= kIrqPending;
    }
}

std::uint16_t Mapper460::configuration_tag() const {
    return static_cast<std::uint16_t>((initial_mirror_ == MirrorMode::Vertical ? 1U : 0U) |
                                      (solder_pad_connected_ ? 2U : 0U));
}

void Mapper460::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(configuration_tag());
    writer.write(registers_.extra);
    writer.write(registers_.bank_select);
    writer.write(registers_.bank_mode);
    writer.write_array(registers_.banks);
    writer.write<Byte>(static_cast<Byte>(registers_.mirror));
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_reload);
    writer.write(registers_.irq_flags);
    writer.write(registers_.ram_flags);
    writer.write_array(chr_ram_);
}

void Mapper460::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto configuration = reader.read<std::uint16_t>();
    Registers next{};
    reader.read(next.extra);
    reader.read(next.bank_select);
    reader.read(next.bank_mode);
    reader.read_array(next.banks);
    const auto mirror = reader.read<Byte>();
    reader.read(next.irq_counter);
    reader.read(next.irq_reload);
    reader.read(next.irq_flags);
    reader.read(next.ram_flags);
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    const bool valid_registers =
        (next.extra & ~kExtraMask) == 0U && next.bank_select <= 7U &&
        (next.bank_mode & 0x3FU) == 0U && next.banks[6U] <= 0x3FU && next.banks[7U] <= 0x3FU &&
        mirror <= static_cast<Byte>(MirrorMode::Vertical) && next.irq_flags <= 7U &&
        ((next.irq_flags & kIrqPending) == 0U || (next.irq_flags & kIrqEnabled) != 0U) &&
        (next.ram_flags & 0x3FU) == 0U;
    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || configuration != configuration_tag() || !valid_registers) {
        reader.fail();
        return;
    }

    next.mirror = static_cast<MirrorMode>(mirror);
    registers_ = next;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
