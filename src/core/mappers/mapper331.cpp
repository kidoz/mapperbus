#include "core/mappers/mapper331.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankBytes = 16384U;
constexpr std::size_t kChrBankBytes = 4096U;
constexpr std::size_t kOuterBankBytes = 128U * 1024U;
constexpr std::size_t kRomBytes = 512U * 1024U;
constexpr std::uint16_t kStateTag = 0x3310U;

} // namespace

// Original implementation from NESdev mapper 331 revision 7849. The Kazzo
// NS03 dumping script in NESdev forum topic 7912, post 292413, corroborates
// the fitted 512 KiB PRG/CHR population.
Mapper331::Mapper331(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> Mapper331::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 331U) {
        return std::unexpected("Mapper331 requires mapper ID 331");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 331 requires NES 2.0 submapper 0");
    }
    if (prg_size != kRomBytes || chr_size != kRomBytes) {
        return std::unexpected("Mapper 331 requires 512 KiB PRG ROM and 512 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 331 supports ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 331 requires ordinary H/V nametable metadata");
    }
    return {};
}

Byte Mapper331::active_inner_bank() const {
    return ppu_a12_ ? inner_bank_b_ : inner_bank_a_;
}

Byte Mapper331::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }

    std::size_t bank = active_inner_bank() & 7U;
    if ((addr & 0x4000U) != 0U) {
        if ((outer_bank_ & 8U) == 0U) {
            bank = 7U;
        } else {
            bank |= 1U;
        }
    }
    const std::size_t offset = static_cast<std::size_t>(outer_bank_ & 3U) * kOuterBankBytes +
                               bank * kPrgBankBytes + (addr & 0x3FFFU);
    return prg_rom_[offset];
}

void Mapper331::write_prg(Address addr, Byte value) {
    switch (addr & 0xE000U) {
    case 0xA000U:
        inner_bank_a_ = value;
        break;
    case 0xC000U:
        inner_bank_b_ = value;
        break;
    case 0xE000U:
        outer_bank_ = value & 0x0FU;
        break;
    default:
        break;
    }
}

Byte Mapper331::read_chr(Address addr) {
    const std::size_t bank = (active_inner_bank() >> 3U) & 0x0FU;
    const std::size_t offset = static_cast<std::size_t>(outer_bank_ & 3U) * kOuterBankBytes +
                               bank * kChrBankBytes + (addr & 0x0FFFU);
    return chr_rom_[offset];
}

void Mapper331::write_chr(Address, Byte) {}

MirrorMode Mapper331::mirror_mode() const {
    return (outer_bank_ & 4U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

bool Mapper331::observes_ppu_address() const {
    return true;
}

void Mapper331::observe_ppu_address(Address addr) {
    ppu_a12_ = (addr & 0x1000U) != 0U;
}

void Mapper331::reset() {
    // Zeroed latches and PPU A12 are deterministic policies; reset wiring is
    // not specified by the pinned article.
    inner_bank_a_ = 0U;
    inner_bank_b_ = 0U;
    outer_bank_ = 0U;
    ppu_a12_ = false;
}

void Mapper331::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(inner_bank_a_);
    writer.write(inner_bank_b_);
    writer.write(outer_bank_);
    writer.write<Byte>(ppu_a12_ ? 1U : 0U);
}

void Mapper331::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto inner_bank_a = reader.read<Byte>();
    const auto inner_bank_b = reader.read<Byte>();
    const auto outer_bank = reader.read<Byte>();
    const auto ppu_a12 = reader.read<Byte>();

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || outer_bank > 0x0FU || ppu_a12 > 1U) {
        reader.fail();
        return;
    }

    inner_bank_a_ = inner_bank_a;
    inner_bank_b_ = inner_bank_b;
    outer_bank_ = outer_bank;
    ppu_a12_ = ppu_a12 != 0U;
}

} // namespace mapperbus::core
