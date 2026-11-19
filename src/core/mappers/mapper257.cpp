#include "core/mappers/mapper257.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgRomSize = 512UZ * 1024UZ;
constexpr std::size_t kCartridgeRamSize = 8192U;
constexpr uint16_t kStateTag = 0xC257U;

} // namespace

Mapper257::Mapper257(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper257::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (!header.is_nes2 || header.mapper_number != 257U ||
        (header.submapper != 0U && header.submapper != 2U))
        return std::unexpected("Mapper 257 component requires NES 2.0 submapper 0 or 2");
    if (prg_size != kPrgRomSize || chr_size != 0U)
        return std::unexpected("Mapper 257 submapper 2 requires 512 KiB PRG ROM and no CHR ROM");
    if (header.has_battery || header.prg_ram_size != kCartridgeRamSize ||
        header.prg_nvram_size != 0U || header.chr_ram_size != kCartridgeRamSize ||
        header.chr_nvram_size != 0U)
        return std::unexpected("Mapper 257 requires 8 KiB volatile PRG RAM and 8 KiB CHR RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 257 requires ordinary H/V CIRAM metadata");
    return {};
}

std::size_t Mapper257::prg_offset(Address addr) const {
    if ((mode_ & 0x10U) != 0U)
        return static_cast<std::size_t>(mode_ & 7U) * 32768U + (addr & 0x7FFFU);
    if ((mode_ & 0x50U) == 0x40U && addr < 0xA000U) {
        const auto bank = static_cast<std::size_t>((mode_ & 0x1FU) | ((mode_ & 0x20U) >> 1U));
        return 0x40000UZ + bank * 8192U + (addr & 0x1FFFU);
    }
    const auto slot = static_cast<std::size_t>((addr - 0x8000U) >> 10U);
    return 0x40000UZ + slot * 8192U + 0x1C00UZ + (addr & 0x03FFU);
}

Byte Mapper257::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U)
        return prg_ram_[addr & 0x1FFFU];
    if (addr < 0x8000U)
        return 0U;
    return prg_rom_[prg_offset(addr)];
}

void Mapper257::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U)
        prg_ram_[addr & 0x1FFFU] = value;
}

bool Mapper257::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Address Mapper257::chr_offset(Address addr) const {
    addr &= 0x1FFFU;
    if ((mode_ & 0x80U) == 0U)
        return addr;
    return static_cast<Address>((addr & 0x0FF7U) | (latched_a0_ ? 0x0008U : 0U) |
                                (latched_a9_ ? 0x1000U : 0U));
}

Byte Mapper257::read_chr(Address addr) {
    return chr_ram_[chr_offset(addr)];
}

void Mapper257::write_chr(Address addr, Byte value) {
    chr_ram_[chr_offset(addr)] = value;
}

MirrorMode Mapper257::mirror_mode() const {
    return (mode_ & 0x08U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

bool Mapper257::maps_expansion(Address addr) const {
    const auto reg = addr & 0xF700U;
    return reg == 0x5000U || reg == 0x5100U || reg == 0x5300U;
}

Byte Mapper257::expansion_read_mask(Address) const {
    return 0U;
}

void Mapper257::write_expansion(Address addr, Byte value) {
    switch (addr & 0xF700U) {
    case 0x5000U:
        mode_ = value;
        break;
    case 0x5100U:
        tape_output_ = (value & 0x02U) != 0U;
        break;
    default:
        break;
    }
}

bool Mapper257::observes_ppu_address() const {
    return true;
}

void Mapper257::observe_ppu_address(Address addr) {
    const bool a13 = (addr & 0x2000U) != 0U;
    if (a13 && !previous_a13_) {
        latched_a0_ = (addr & 0x0001U) != 0U;
        latched_a9_ = (addr & 0x0200U) != 0U;
    }
    previous_a13_ = a13;
}

void Mapper257::reset() {
    mode_ = 0U;
    tape_output_ = false;
    latched_a0_ = false;
    latched_a9_ = false;
    previous_a13_ = false;
}

void Mapper257::power_on() {
    reset();
    prg_ram_.fill(0U);
    chr_ram_.fill(0U);
}

void Mapper257::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(mode_);
    writer.write(static_cast<Byte>(tape_output_));
    writer.write(static_cast<Byte>((latched_a0_ ? 1U : 0U) | (latched_a9_ ? 2U : 0U) |
                                   (previous_a13_ ? 4U : 0U)));
    writer.write_array(prg_ram_);
    writer.write_array(chr_ram_);
}

void Mapper257::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto mode = reader.read<Byte>();
    const auto tape = reader.read<Byte>();
    const auto pins = reader.read<Byte>();
    std::array<Byte, kCartridgeRamSize> prg_ram{};
    std::array<Byte, kCartridgeRamSize> chr_ram{};
    reader.read_array(prg_ram);
    reader.read_array(chr_ram);
    if (!reader.ok() || tag != kStateTag || tape > 1U || pins > 7U) {
        reader.fail();
        return;
    }
    mode_ = mode;
    tape_output_ = tape != 0U;
    latched_a0_ = (pins & 1U) != 0U;
    latched_a9_ = (pins & 2U) != 0U;
    previous_a13_ = (pins & 4U) != 0U;
    prg_ram_ = prg_ram;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
