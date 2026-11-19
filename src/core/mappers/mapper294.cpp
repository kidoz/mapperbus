#include "core/mappers/mapper294.hpp"

#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 2U * 1024U * 1024U;
constexpr std::size_t kOuterBankSize = 128U * 1024U;
constexpr std::size_t kInnerBankSize = 16U * 1024U;
constexpr std::size_t kChrRamSize = 8192U;
constexpr Address kOuterMask = 0xC100U;
constexpr Address kOuterValue = 0x4100U;
constexpr std::uint16_t kStateTag = 0xFE00U;

} // namespace

// Original implementation from NESdev mapper 294 r19317. The article identifies
// the contents as UNROM games; the inner latch and AND bus conflicts follow the
// UNROM/UxROM hardware description. The fixed bank is local to each 128 KiB block.
Mapper294::Mapper294(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)) {
    chr_rom.clear();
    power_on();
}

Result<void> Mapper294::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 294U || !header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 294 requires NES 2.0 mapper 294 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != 0U) {
        return std::unexpected("Mapper 294 requires exactly 2 MiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 294 requires exactly 8 KiB volatile CHR RAM and no other RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 294 requires H/V header mirroring");
    }
    return {};
}

bool Mapper294::is_outer_register(Address addr) {
    return (addr & kOuterMask) == kOuterValue;
}

Byte Mapper294::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    const std::size_t inner = addr < 0xC000U ? inner_bank_ : 7U;
    const std::size_t offset = static_cast<std::size_t>(outer_bank_) * kOuterBankSize +
                               inner * kInnerBankSize + (addr & 0x3FFFU);
    return prg_rom_[offset];
}

void Mapper294::write_outer(Byte value) {
    outer_bank_ = value & 0x0FU;
    mirror_ = (value & 0x10U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper294::write_prg(Address addr, Byte value) {
    if (is_outer_register(addr) && addr < 0x8000U) {
        write_outer(value);
        return;
    }
    if (addr < 0x8000U) {
        return;
    }
    const Byte latched = value & read_prg(addr);
    inner_bank_ = latched & 0x07U;
}

Byte Mapper294::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper294::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper294::mirror_mode() const {
    return mirror_;
}

bool Mapper294::maps_expansion(Address addr) const {
    return addr >= 0x4020U && addr < 0x6000U && is_outer_register(addr);
}

Byte Mapper294::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0U : 0xFFU;
}

Byte Mapper294::read_expansion(Address) {
    return 0U;
}

void Mapper294::write_expansion(Address addr, Byte value) {
    if (maps_expansion(addr)) {
        write_outer(value);
    }
}

void Mapper294::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper294::reset() {
    // Electrical reset is unspecified; use deterministic zero latches and retain CHR RAM.
    outer_bank_ = 0U;
    inner_bank_ = 0U;
    mirror_ = MirrorMode::Vertical;
}

void Mapper294::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<Byte>(outer_bank_ | (mirror_ == MirrorMode::Horizontal ? 0x10U : 0U)));
    writer.write(inner_bank_);
    writer.write_array(chr_ram_);
}

void Mapper294::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto outer = reader.read<Byte>();
    const auto inner = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr{};
    reader.read_array(chr);
    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() || (outer & ~0x1FU) != 0U ||
        (inner & ~0x07U) != 0U) {
        reader.fail();
        return;
    }
    write_outer(outer);
    inner_bank_ = inner;
    chr_ram_ = chr;
}

} // namespace mapperbus::core
