#include "core/mappers/mapper365.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x200000UZ;
constexpr std::size_t kPrgRamSize = 0x2000UZ;
constexpr std::size_t kMinChrRamSize = 0x2000UZ;
constexpr std::size_t kMaxChrRamSize = 0x040000UZ;
constexpr std::uint16_t kStateTag = 0x3650U;

} // namespace

// NESdev mapper 365 oldid 7963 specifies four full-byte PRG and eight full-byte
// CHR-RAM selectors. Geometry bounds describe those decoder widths, not measured
// PCB populations. The mirroring register and several expansion devices remain
// unidentified: their writes are retained, unknown reads float, and CIRAM keeps
// its header-selected H/V arrangement. No IRQ or expansion audio is documented.
// Reset clears latches and retains RAM; cold power clears RAM deterministically.
Mapper365::Mapper365(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), prg_ram_(kPrgRamSize, 0U), chr_ram_(header.chr_ram_size, 0U),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper365::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 365U) {
        return std::unexpected("Mapper 365 requires mapper ID 365");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 365 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        chr_size != 0U) {
        return std::unexpected(
            "Mapper 365 requires power-of-two 32 KiB-2 MiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != kPrgRamSize || header.prg_nvram_size != 0U ||
        !std::has_single_bit(header.chr_ram_size) || header.chr_ram_size < kMinChrRamSize ||
        header.chr_ram_size > kMaxChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 365 requires 8 KiB volatile PRG RAM, 8-256 KiB power-of-two CHR RAM, "
            "and no NVRAM or battery");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 365 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper365::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        return prg_ram_[addr - 0x6000U];
    }
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const auto offset = static_cast<std::size_t>(registers_.prg_banks[slot]) * kPrgBankSize +
                        (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper365::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        prg_ram_[addr - 0x6000U] = value;
        return;
    }
    if (addr < 0x8000U) {
        return;
    }
    if ((addr & 0xE000U) == 0x8000U) {
        registers_.prg_banks[addr & 3U] = (addr & 3U) == 3U ? value | 1U : value;
        return;
    }
    if ((addr & 0xE000U) == 0xA000U) {
        registers_.chr_banks[addr & 7U] = value;
        return;
    }

    const unsigned index = ((addr & 0x2000U) >> 12U) | (addr & 1U);
    registers_.unknown_prg[index] = value;
}

bool Mapper365::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper365::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper365::read_chr(Address addr) {
    if (chr_ram_.empty()) {
        return 0U;
    }
    const auto pattern_addr = static_cast<Address>(addr & 0x1FFFU);
    const unsigned slot = pattern_addr / kChrBankSize;
    const auto offset = static_cast<std::size_t>(registers_.chr_banks[slot]) * kChrBankSize +
                        (pattern_addr & (kChrBankSize - 1U));
    return chr_ram_[offset % chr_ram_.size()];
}

void Mapper365::write_chr(Address addr, Byte value) {
    if (chr_ram_.empty()) {
        return;
    }
    const auto pattern_addr = static_cast<Address>(addr & 0x1FFFU);
    const unsigned slot = pattern_addr / kChrBankSize;
    const auto offset = static_cast<std::size_t>(registers_.chr_banks[slot]) * kChrBankSize +
                        (pattern_addr & (kChrBankSize - 1U));
    chr_ram_[offset % chr_ram_.size()] = value;
}

MirrorMode Mapper365::mirror_mode() const {
    return initial_mirror_;
}

bool Mapper365::maps_expansion(Address addr) const {
    return addr == 0x4902U;
}

Byte Mapper365::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0x80U : 0U;
}

Byte Mapper365::read_expansion(Address) {
    return 0U;
}

void Mapper365::write_expansion(Address addr, Byte value) {
    switch (addr) {
    case 0x4111U:
        registers_.unknown_4111 = value;
        return;
    case 0x4900U:
    case 0x4901U:
        registers_.printer_output[addr - 0x4900U] = value;
        return;
    case 0x4904U:
        registers_.keyboard_row = value;
        return;
    case 0x4905U:
        registers_.unknown_4905 = value;
        return;
    default:
        break;
    }
    if (addr >= 0x4910U && addr <= 0x491FU) {
        registers_.unknown_491x[addr - 0x4910U] = value;
    }
}

void Mapper365::power_on() {
    reset();
    std::ranges::fill(prg_ram_, 0U);
    std::ranges::fill(chr_ram_, 0U);
}

void Mapper365::reset() {
    registers_ = Registers{};
}

void Mapper365::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(static_cast<std::uint32_t>(prg_ram_.size()));
    writer.write(static_cast<std::uint32_t>(chr_ram_.size()));
    writer.write<Byte>(initial_mirror_ == MirrorMode::Vertical ? 1U : 0U);
    writer.write_array(registers_.prg_banks);
    writer.write_array(registers_.chr_banks);
    writer.write_array(registers_.unknown_prg);
    writer.write_array(registers_.printer_output);
    writer.write(registers_.keyboard_row);
    writer.write(registers_.unknown_4905);
    writer.write(registers_.unknown_4111);
    writer.write_array(registers_.unknown_491x);
    writer.write_bytes(prg_ram_);
    writer.write_bytes(chr_ram_);
}

void Mapper365::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto prg_ram_size = reader.read<std::uint32_t>();
    const auto chr_ram_size = reader.read<std::uint32_t>();
    const auto initial_mirror = reader.read<Byte>();
    Registers next;
    reader.read_array(next.prg_banks);
    reader.read_array(next.chr_banks);
    reader.read_array(next.unknown_prg);
    reader.read_array(next.printer_output);
    reader.read(next.keyboard_row);
    reader.read(next.unknown_4905);
    reader.read(next.unknown_4111);
    reader.read_array(next.unknown_491x);
    std::vector<Byte> prg_ram;
    std::vector<Byte> chr_ram;
    reader.read_bytes(prg_ram, kPrgRamSize);
    reader.read_bytes(chr_ram, kMaxChrRamSize);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() || chr_size != 0U ||
        prg_ram_size != prg_ram_.size() || chr_ram_size != chr_ram_.size() || initial_mirror > 1U ||
        (initial_mirror != 0U) != (initial_mirror_ == MirrorMode::Vertical) ||
        (next.prg_banks[3U] & 1U) == 0U || prg_ram.size() != prg_ram_.size() ||
        chr_ram.size() != chr_ram_.size()) {
        reader.fail();
        return;
    }
    registers_ = next;
    prg_ram_ = std::move(prg_ram);
    chr_ram_ = std::move(chr_ram);
}

} // namespace mapperbus::core
