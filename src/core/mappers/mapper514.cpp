#include "core/mappers/mapper514.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x200000UZ;
constexpr std::size_t kPrgBankSize = 0x008000UZ;
constexpr std::size_t kCartridgeRamSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x001000UZ;
constexpr std::uint16_t kStateTag = 0x5140U;
constexpr Byte kPrgBankMask = 0x3FU;
constexpr Byte kArrangementMask = 0x40U;
constexpr Byte kChrModeMask = 0x80U;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 514 oldid 22322.
// Reset is unspecified; restoring the header arrangement and retaining RAM are
// deterministic soft-reset policies. Cold power clears both volatile RAM arrays.
Mapper514::Mapper514(const INesHeader& header,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> /*chr_rom*/)
    : prg_rom_(std::move(prg_rom)), initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper514::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 514U) {
        return std::unexpected("Mapper 514 requires mapper ID 514");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 514 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        chr_size != 0U) {
        return std::unexpected(
            "Mapper 514 requires power-of-two 32 KiB-2 MiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != kCartridgeRamSize ||
        header.prg_nvram_size != 0U || header.chr_ram_size != kCartridgeRamSize ||
        header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 514 requires 8 KiB volatile PRG RAM and 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 514 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 514 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper514::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        return wram_[addr & (kCartridgeRamSize - 1U)];
    }
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    const auto offset = static_cast<std::size_t>(registers_.mode & kPrgBankMask) * kPrgBankSize +
                        (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper514::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        wram_[addr & (kCartridgeRamSize - 1U)] = value;
        return;
    }
    if (addr == 0x8000U) {
        registers_.mode = value;
    }
}

bool Mapper514::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper514::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

std::size_t Mapper514::chr_offset(Address addr) const {
    if (addr >= kChrBankSize || (registers_.mode & kChrModeMask) == 0U) {
        return addr;
    }
    const bool horizontal_arrangement = (registers_.mode & kArrangementMask) != 0U;
    const auto bank =
        horizontal_arrangement ? (registers_.nametable & 1U) : ((registers_.nametable >> 1U) & 1U);
    return static_cast<std::size_t>(bank) * kChrBankSize + addr;
}

Byte Mapper514::read_chr(Address addr) {
    return addr < kCartridgeRamSize ? chr_ram_[chr_offset(addr)] : 0U;
}

void Mapper514::write_chr(Address addr, Byte value) {
    if (addr < kCartridgeRamSize) {
        chr_ram_[addr] = value;
    }
}

MirrorMode Mapper514::mirror_mode() const {
    // The register names arrangement, the inverse of MirrorMode's mirroring names.
    return (registers_.mode & kArrangementMask) != 0U ? MirrorMode::Vertical
                                                      : MirrorMode::Horizontal;
}

bool Mapper514::observes_ppu_address() const {
    return true;
}

void Mapper514::observe_ppu_address(Address addr) {
    if (addr >= 0x2000U && addr < 0x3F00U) {
        registers_.nametable = static_cast<Byte>((addr >> 10U) & 3U);
    }
}

void Mapper514::power_on() {
    reset();
    wram_.fill(0U);
    chr_ram_.fill(0U);
}

void Mapper514::reset() {
    registers_ = Registers{};
    registers_.mode = initial_mirror_ == MirrorMode::Vertical ? kArrangementMask : 0U;
}

std::uint16_t Mapper514::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper514::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(registers_.mode);
    writer.write(registers_.nametable);
    writer.write_array(wram_);
    writer.write_array(chr_ram_);
}

void Mapper514::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.mode);
    reader.read(next.nametable);
    std::array<Byte, kCartridgeRamSize> wram{};
    std::array<Byte, kCartridgeRamSize> chr_ram{};
    reader.read_array(wram);
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() || chr_size != 0U ||
        next.nametable > 3U) {
        reader.fail();
        return;
    }

    registers_ = next;
    wram_ = wram;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
