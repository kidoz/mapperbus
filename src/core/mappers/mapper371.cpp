#include "core/mappers/mapper371.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kFirstChipSize = 0x010000UZ;
constexpr std::size_t kSecondChipSize = 0x080000UZ;
constexpr std::size_t kPrgSize = kFirstChipSize + kSecondChipSize;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kCartridgeRamSize = 0x002000UZ;
constexpr Address kRegisterMask = 0xFF00U;
constexpr Address kMainRegister = 0x5000U;
constexpr Address kAuxiliaryRegister = 0x5100U;
constexpr Address kTapeRegister = 0x5500U;
constexpr std::uint16_t kStateTag = 0x3711U;

} // namespace

// NESdev mapper 371 oldid 7978 defines the two physical PRG chips, FF00 register
// decoding and nametable-read-controlled CHR pins. Unknown source encodings and
// an empty expansion slot are left undriven rather than aliased to onboard ROM.
// Soft reset restores the documented power-on registers and retains RAM; cold
// power clears volatile RAM deterministically. Tape input is an external line.
Mapper371::Mapper371(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper371::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 371U) {
        return std::unexpected("Mapper 371 requires mapper ID 371");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 371 requires NES 2.0 submapper 0");
    }
    if ((prg_size != kPrgSize && prg_size != kFirstChipSize) || chr_size != 0U) {
        return std::unexpected("Mapper 371 requires a 64 KiB system ROM, optionally with the 512 "
                               "KiB game ROM, and no CHR ROM");
    }
    if (header.has_trainer || header.has_battery || header.prg_ram_size != kCartridgeRamSize ||
        header.prg_nvram_size != 0U || header.chr_ram_size != kCartridgeRamSize ||
        header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 371 requires 8 KiB volatile PRG RAM and 8 KiB CHR RAM without NVRAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 371 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Mapper371::PrgSource Mapper371::prg_source() const {
    return static_cast<PrgSource>((registers_.main >> 4U) & 7U);
}

Byte Mapper371::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        return prg_ram_[addr & 0x1FFFU];
    }
    if (addr < 0x8000U) {
        return 0U;
    }

    if (prg_rom_.size() == kPrgSize && prg_source() == PrgSource::SecondChip &&
        (registers_.auxiliary & 1U) == 0U && (addr & 0xFF00U) == 0xD100U) {
        // The mapper interface has no console-reset request seam. Perform the
        // cartridge-local part of the documented protection reset here.
        reset();
    }

    const auto within_bank = static_cast<std::size_t>(addr & 0x3FFFU);
    switch (prg_source()) {
    case PrgSource::FirstChip: {
        const unsigned bank = addr < 0xC000U ? registers_.main & 3U : 3U;
        return prg_rom_[bank * kPrgBankSize + within_bank];
    }
    case PrgSource::SecondChip: {
        if (prg_rom_.size() != kPrgSize)
            return 0;
        const unsigned bank =
            (registers_.main & 0x0FU) | ((registers_.auxiliary & 1U) != 0U ? 0x10U : 0U);
        return prg_rom_[kFirstChipSize + bank * kPrgBankSize + within_bank];
    }
    case PrgSource::ExpansionSlot:
    default:
        return 0U;
    }
}

void Mapper371::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        prg_ram_[addr & 0x1FFFU] = value;
    }
}

bool Mapper371::maps_prg(Address addr) const {
    if (addr >= 0x6000U && addr < 0x8000U) {
        return true;
    }
    if (addr < 0x8000U) {
        return false;
    }
    const auto source = prg_source();
    return source == PrgSource::FirstChip ||
           (source == PrgSource::SecondChip && prg_rom_.size() == kPrgSize);
}

Byte Mapper371::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Address Mapper371::chr_address(Address addr) const {
    addr &= 0x1FFFU;
    if ((registers_.main & 0x80U) == 0U) {
        return addr;
    }
    return static_cast<Address>((addr & 0x0FF7U) |
                                ((registers_.nametable_pins & 1U) != 0U ? 0x0008U : 0U) |
                                ((registers_.nametable_pins & 2U) != 0U ? 0x1000U : 0U));
}

Byte Mapper371::read_chr(Address addr) {
    return chr_ram_[chr_address(addr)];
}

void Mapper371::write_chr(Address addr, Byte value) {
    chr_ram_[chr_address(addr)] = value;
}

MirrorMode Mapper371::mirror_mode() const {
    return (registers_.auxiliary & 2U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
}

bool Mapper371::maps_expansion(Address addr) const {
    const auto decoded = static_cast<Address>(addr & kRegisterMask);
    return decoded == kMainRegister || decoded == kAuxiliaryRegister || decoded == kTapeRegister;
}

Byte Mapper371::expansion_read_mask(Address addr) const {
    return (addr & kRegisterMask) == kTapeRegister ? 0x04U : 0U;
}

Byte Mapper371::read_expansion(Address addr) {
    return (addr & kRegisterMask) == kTapeRegister && tape_input_ ? 0x04U : 0U;
}

void Mapper371::write_expansion(Address addr, Byte value) {
    switch (addr & kRegisterMask) {
    case kMainRegister:
        registers_.main = value;
        break;
    case kAuxiliaryRegister:
        registers_.auxiliary = value & 3U;
        break;
    default:
        break;
    }
}

bool Mapper371::observes_ppu_reads() const {
    return true;
}

void Mapper371::observe_ppu_read(Address addr) {
    addr &= 0x3FFFU;
    if (addr < 0x2000U || addr >= 0x3F00U || (addr & 0x03FFU) >= 0x03C0U) {
        return;
    }
    registers_.nametable_pins = static_cast<Byte>((addr & 1U) | ((addr >> 8U) & 2U));
}

void Mapper371::power_on() {
    reset();
    prg_ram_.fill(0U);
    chr_ram_.fill(0U);
}

void Mapper371::reset() {
    registers_ = Registers{};
    keyboard_.reset();
}

void Mapper371::set_tape_input(bool active) {
    tape_input_ = active;
}

bool Mapper371::tape_output() const {
    return prg_source() == PrgSource::FirstChip && (registers_.auxiliary & 1U) != 0U;
}

void Mapper371::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(registers_.main);
    writer.write(registers_.auxiliary);
    writer.write(registers_.nametable_pins);
    writer.write<Byte>(tape_input_ ? 1U : 0U);
    writer.write_array(prg_ram_);
    writer.write_array(chr_ram_);
    keyboard_.save_state(writer);
}

void Mapper371::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.main);
    reader.read(next.auxiliary);
    reader.read(next.nametable_pins);
    const auto tape_input = reader.read<Byte>();
    std::array<Byte, kCartridgeRamSize> next_prg_ram{};
    std::array<Byte, kCartridgeRamSize> next_chr_ram{};
    reader.read_array(next_prg_ram);
    reader.read_array(next_chr_ram);
    auto keyboard = keyboard_;
    keyboard.load_state(reader);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() || chr_size != 0U ||
        next.auxiliary > 3U || next.nametable_pins > 3U || tape_input > 1U) {
        reader.fail();
        return;
    }

    keyboard_ = keyboard;
    registers_ = next;
    tape_input_ = tape_input != 0U;
    prg_ram_ = next_prg_ram;
    chr_ram_ = next_chr_ram;
}

} // namespace mapperbus::core
