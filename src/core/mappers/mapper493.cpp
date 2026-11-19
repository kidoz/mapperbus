#include "core/mappers/mapper493.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kRomSize = 0x100000UZ;
constexpr std::size_t kOuterBankSize = 0x010000UZ;
constexpr std::size_t kPrgWindowSize = 0x008000UZ;
constexpr std::size_t kChrWindowSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x4930U;
constexpr Byte kOuterMask = 0x0FU;
constexpr Byte kInnerMask = 0x71U;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 493 oldid 24546.
// Electrical reset is unspecified; clearing both latches is a deterministic policy.
Mapper493::Mapper493(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> Mapper493::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 493U) {
        return std::unexpected("Mapper 493 requires mapper ID 493");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 493 requires NES 2.0 submapper 0");
    }
    if (prg_size != kRomSize || chr_size != kRomSize) {
        return std::unexpected("Mapper 493 requires exactly 1 MiB PRG ROM and 1 MiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 493 supports ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal) {
        return std::unexpected("Mapper 493 requires hard-wired Horizontal nametable metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 493 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper493::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    const auto offset = static_cast<std::size_t>(registers_.outer) * kOuterBankSize +
                        static_cast<std::size_t>(registers_.inner & 1U) * kPrgWindowSize +
                        (addr & (kPrgWindowSize - 1U));
    return offset < prg_rom_.size() ? prg_rom_[offset] : 0U;
}

void Mapper493::write_prg(Address addr, Byte value) {
    if ((addr & 0xE000U) == 0x6000U) {
        registers_.outer = value & kOuterMask;
        return;
    }
    if (addr >= 0x8000U) {
        registers_.inner = value & read_prg(addr) & kInnerMask;
    }
}

bool Mapper493::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper493::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper493::read_chr(Address addr) {
    if (addr >= kChrWindowSize) {
        return 0U;
    }
    const auto offset = static_cast<std::size_t>(registers_.outer) * kOuterBankSize +
                        static_cast<std::size_t>((registers_.inner >> 4U) & 7U) * kChrWindowSize +
                        addr;
    return offset < chr_rom_.size() ? chr_rom_[offset] : 0U;
}

void Mapper493::write_chr(Address, Byte) {}

MirrorMode Mapper493::mirror_mode() const {
    return MirrorMode::Horizontal;
}

void Mapper493::reset() {
    registers_ = Registers{};
}

void Mapper493::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(registers_.outer);
    writer.write(registers_.inner);
}

void Mapper493::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.outer);
    reader.read(next.inner);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || (next.outer & ~kOuterMask) != 0U ||
        (next.inner & ~kInnerMask) != 0U) {
        reader.fail();
        return;
    }

    registers_ = next;
}

} // namespace mapperbus::core
