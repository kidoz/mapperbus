#include "core/mappers/mapper342.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x4000UZ;
constexpr std::size_t kMaxPrgSize = 0x8000000UZ;
constexpr std::size_t kMinChrRamSize = 0x2000UZ;
constexpr std::size_t kMaxChrRamSize = 0x80000UZ;
constexpr std::size_t kWramSize = 0x8000UZ;
constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::uint16_t kStateTag = 0x3420U;

bool native_register(Address addr) {
    return (addr & 0xF000U) == 0x5000U;
}

} // namespace

// Native decoding follows NESdev mapper 342 oldid 22755. Optional nonvolatile
// FRAM population is confirmed by the COOLGIRL hardware project. Unsupported
// modes return open CPU bus or zero CHR data; no reserved behavior is inferred.
Mapper342::Mapper342(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), wram_(header.prg_nvram_size), chr_ram_(header.chr_ram_size) {
    power_on();
}

Result<void> Mapper342::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 342U) {
        return std::unexpected("Mapper 342 requires mapper ID 342");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 342 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize) {
        return std::unexpected("Mapper 342 requires power-of-two 16 KiB-128 MiB PRG ROM");
    }
    if (chr_size != 0U || !std::has_single_bit(header.chr_ram_size) ||
        header.chr_ram_size < kMinChrRamSize || header.chr_ram_size > kMaxChrRamSize ||
        header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 342 requires 8-512 KiB volatile CHR RAM and no CHR ROM/NVRAM");
    }
    if (header.prg_ram_size != 0U ||
        (header.prg_nvram_size != 0U && header.prg_nvram_size != kWramSize) ||
        header.has_battery != (header.prg_nvram_size == kWramSize)) {
        return std::unexpected("Mapper 342 supports only optional 32 KiB nonvolatile FRAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical &&
        header.mirror_mode != MirrorMode::FourScreen) {
        return std::unexpected("Mapper 342 requires H/V or four-screen cartridge metadata");
    }
    return {};
}

void Mapper342::sync_native_banks() {
    state_.prg_banks[0] = static_cast<Byte>((state_.registers[5] >> 1U) & 0x3EU);
    state_.chr_banks[0] = static_cast<std::uint16_t>(((state_.registers[3] & 0x1FU) << 3U) |
                                                     ((state_.registers[5] & 0x80U) << 1U));
}

bool Mapper342::wram_mapped(Address addr) const {
    return addr >= 0x6000U && addr < 0x8000U && !wram_.empty() &&
           (state_.registers[7] & 0x01U) != 0U;
}

std::optional<std::uint32_t> Mapper342::mapped_prg_offset(Address addr) const {
    const auto mode = static_cast<unsigned>((state_.registers[3] >> 5U) & 7U);
    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    unsigned bank = 0U;
    switch (mode) {
    case 0U:
    case 1U: {
        const bool first_half = slot < 2U;
        const unsigned index = (mode == 0U) == first_half ? 0U : 2U;
        bank = (state_.prg_banks[index] & 0xFEU) | (slot & 1U);
        break;
    }
    case 4U:
    case 5U: {
        constexpr std::array<std::array<unsigned, 4>, 2> slots{
            {{0U, 1U, 2U, 3U}, {2U, 1U, 0U, 3U}}};
        bank = state_.prg_banks[slots[mode - 4U][slot]];
        break;
    }
    case 6U:
    case 7U: {
        const unsigned index = mode == 6U ? 1U : 0U;
        bank = (state_.prg_banks[index] & 0xFCU) | slot;
        break;
    }
    default:
        return std::nullopt;
    }

    std::uint32_t offset = bank * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    const std::uint32_t mask = static_cast<std::uint32_t>(state_.registers[2] & 0x7FU) << 14U;
    const std::uint32_t base = (static_cast<std::uint32_t>(state_.registers[0]) << 22U) |
                               (static_cast<std::uint32_t>(state_.registers[1]) << 14U);
    offset = (offset & ~mask) | base;
    return offset % prg_rom_.size();
}

std::optional<std::uint32_t> Mapper342::mapped_chr_offset(Address addr) const {
    const auto mode = static_cast<unsigned>(state_.registers[4] & 7U);
    const auto slot = static_cast<unsigned>((addr >> 10U) & 7U);
    unsigned bank = 0U;
    const auto paired = [&](unsigned index) {
        return (state_.chr_banks[index] & 0x1FEU) | (slot & 1U);
    };
    switch (mode) {
    case 0U:
        bank = (state_.chr_banks[0] & 0x1F8U) | slot;
        break;
    case 2U:
        if (slot < 2U) {
            bank = paired(0U);
        } else if (slot < 4U) {
            bank = paired(2U);
        } else {
            bank = state_.chr_banks[slot];
        }
        break;
    case 3U:
        if (slot < 4U) {
            bank = state_.chr_banks[slot];
        } else if (slot < 6U) {
            bank = paired(0U);
        } else {
            bank = paired(2U);
        }
        break;
    case 4U:
        bank = (state_.chr_banks[slot < 4U ? 0U : 4U] & 0x1FCU) | (slot & 3U);
        break;
    case 6U:
        bank = paired(slot / 2U * 2U);
        break;
    case 7U:
        bank = state_.chr_banks[slot];
        break;
    default:
        return std::nullopt;
    }

    std::uint32_t offset = bank * kChrBankSize + (addr & (kChrBankSize - 1U));
    const std::uint32_t mask = (static_cast<std::uint32_t>(state_.registers[2] & 0x80U) << 11U) |
                               (static_cast<std::uint32_t>(state_.registers[4] & 0x1FU) << 13U);
    return (offset & ~mask) % chr_ram_.size();
}

Byte Mapper342::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return wram_mapped(addr) ? wram_[(state_.registers[5] & 3U) * 0x2000U + (addr & 0x1FFFU)]
                                 : 0U;
    }
    const auto offset = mapped_prg_offset(addr);
    return offset ? prg_rom_[*offset] : 0U;
}

void Mapper342::write_prg(Address addr, Byte value) {
    if (wram_mapped(addr)) {
        wram_[(state_.registers[5] & 3U) * 0x2000U + (addr & 0x1FFFU)] = value;
    }
    // Only mapper code zero (NROM) has no inner PRG-register writes.
}

bool Mapper342::maps_prg(Address addr) const {
    return addr >= 0x8000U || wram_mapped(addr);
}

Byte Mapper342::prg_read_mask(Address addr) const {
    if (addr < 0x8000U) {
        return wram_mapped(addr) ? 0xFFU : 0U;
    }
    return mapped_prg_offset(addr) ? 0xFFU : 0U;
}

Byte Mapper342::read_chr(Address addr) {
    const auto offset = mapped_chr_offset(addr);
    return offset ? chr_ram_[*offset] : 0U;
}

void Mapper342::write_chr(Address addr, Byte value) {
    if ((state_.registers[7] & 0x02U) == 0U) {
        return;
    }
    if (const auto offset = mapped_chr_offset(addr)) {
        chr_ram_[*offset] = value;
    }
}

MirrorMode Mapper342::mirror_mode() const {
    const Byte control = state_.registers[7];
    if ((control & 0x10U) != 0U) {
        return MirrorMode::FourScreen;
    }
    switch ((control >> 2U) & 3U) {
    case 0U:
        return MirrorMode::Vertical;
    case 1U:
        return MirrorMode::Horizontal;
    case 2U:
        return MirrorMode::SingleLower;
    default:
        return MirrorMode::SingleUpper;
    }
}

bool Mapper342::maps_expansion(Address addr) const {
    return addr >= 0x5000U && addr < 0x6000U;
}

Byte Mapper342::expansion_read_mask(Address) const {
    return 0U;
}

Byte Mapper342::read_expansion(Address) {
    return 0U;
}

void Mapper342::write_expansion(Address addr, Byte value) {
    if (!native_register(addr) || (state_.registers[7] & 0x80U) != 0U) {
        return;
    }
    state_.registers[addr & 7U] = value;
    sync_native_banks();
}

void Mapper342::power_on() {
    reset();
    std::ranges::fill(chr_ram_, 0U);
}

void Mapper342::reset() {
    state_ = RuntimeState{};
    state_.prg_banks = {0U, 0xFDU, 0xFEU, 0xFFU};
    state_.chr_banks = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
}

std::span<const Byte> Mapper342::battery_ram() const {
    return wram_;
}

void Mapper342::set_battery_ram(std::span<const Byte> data) {
    if (data.size() == wram_.size()) {
        std::ranges::copy(data, wram_.begin());
    }
}

void Mapper342::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(wram_.size()));
    writer.write(static_cast<std::uint32_t>(chr_ram_.size()));
    writer.write_array(state_.registers);
    writer.write_array(state_.prg_banks);
    writer.write_array(state_.chr_banks);
    writer.write_bytes(wram_);
    writer.write_bytes(chr_ram_);
}

void Mapper342::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto wram_size = reader.read<std::uint32_t>();
    const auto chr_ram_size = reader.read<std::uint32_t>();
    RuntimeState next;
    reader.read_array(next.registers);
    reader.read_array(next.prg_banks);
    reader.read_array(next.chr_banks);
    std::vector<Byte> wram;
    std::vector<Byte> chr_ram;
    reader.read_bytes(wram, kWramSize);
    reader.read_bytes(chr_ram, kMaxChrRamSize);

    RuntimeState expected;
    expected.registers = next.registers;
    expected.prg_banks = {0U, 0xFDU, 0xFEU, 0xFFU};
    expected.chr_banks = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
    expected.prg_banks[0] = static_cast<Byte>((next.registers[5] >> 1U) & 0x3EU);
    expected.chr_banks[0] = static_cast<std::uint16_t>(((next.registers[3] & 0x1FU) << 3U) |
                                                       ((next.registers[5] & 0x80U) << 1U));

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        wram_size != wram_.size() || chr_ram_size != chr_ram_.size() ||
        wram.size() != wram_.size() || chr_ram.size() != chr_ram_.size() ||
        next.prg_banks != expected.prg_banks || next.chr_banks != expected.chr_banks) {
        reader.fail();
        return;
    }

    state_ = next;
    wram_ = std::move(wram);
    chr_ram_ = std::move(chr_ram);
}

} // namespace mapperbus::core
