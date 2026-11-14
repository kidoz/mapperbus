#include "core/mappers/action52.hpp"

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr Address kAddressMask = 0x3FEFU;
constexpr std::size_t kThreeChipSize = 0x180000U;
} // namespace

// Original implementation from NESdev mapper 228 r5784 and mark_k's board
// examination (2014-01-03, forum post 123220). ROM geometry selects the fitted
// population: packed chips 0/1/3 or one always-enabled chip. No rumored 4x4 RAM.
Action52::Action52(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      three_chips_(prg_rom_.size() == kThreeChipSize) {}

Result<void> Action52::validate_geometry(const INesHeader& h,
                                         std::size_t prg_size,
                                         std::size_t chr_size) {
    if (h.is_nes2 && h.submapper != 0U)
        return std::unexpected("Mapper 228 supports submapper 0");
    if ((prg_size != kThreeChipSize &&
         (prg_size < 0x4000U || prg_size > 0x80000U || prg_size % 0x4000U != 0U)) ||
        chr_size < 0x2000U || chr_size > 0x80000U || chr_size % 0x2000U != 0U)
        return std::unexpected("Mapper 228 requires 16-512 KiB single-chip or 1536 KiB packed PRG "
                               "ROM and 8-512 KiB CHR ROM");
    if (h.has_battery || h.chr_ram_size != 0U || h.chr_nvram_size != 0U ||
        (h.is_nes2 && (h.prg_ram_size != 0U || h.prg_nvram_size != 0U)))
        return std::unexpected("Mapper 228 has no cartridge RAM or battery storage");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 228 supports ordinary H/V CIRAM");
    return {};
}

Byte Action52::prg_read_mask(Address addr) const {
    return addr >= 0x8000U && three_chips_ && ((address_ >> 11U) & 3U) == 2U ? 0U : 0xFFU;
}

Byte Action52::read_prg(Address addr) {
    if (addr < 0x8000U || prg_read_mask(addr) == 0U)
        return 0U;
    unsigned chip = 0U;
    if (three_chips_) {
        chip = (address_ >> 11U) & 3U;
        if (chip == 3U)
            chip = 2U; // Input images pack physical chips 0, 1 and 3 consecutively.
    }
    unsigned page = (address_ >> 6U) & 31U;
    if ((address_ & 0x20U) == 0U)
        page = (page & 30U) | ((addr >> 14U) & 1U);
    const auto offset = chip * 0x80000U + page * 0x4000U + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Action52::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U) {
        address_ = addr & kAddressMask;
        data_ = value & 3U;
    }
}

Byte Action52::read_chr(Address addr) {
    const auto bank = (address_ & 15U) * 4U + data_;
    return chr_rom_[(bank * 0x2000U + (addr & 0x1FFFU)) % chr_rom_.size()];
}

void Action52::write_chr(Address /*addr*/, Byte /*value*/) {}

MirrorMode Action52::mirror_mode() const {
    return (address_ & 0x2000U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Action52::reset() {
    // Model the reset-time write to $8000 expected by the published notes.
    address_ = 0U;
    data_ = 0U;
}

uint16_t Action52::state_tag() const {
    return static_cast<uint16_t>(0xE400U | (three_chips_ ? 1U : 0U));
}

void Action52::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(address_);
    writer.write(data_);
}

void Action52::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto address = reader.read<Address>();
    const auto data = reader.read<Byte>();
    if (!reader.ok() || tag != state_tag() || (address & ~kAddressMask) != 0U || data > 3U) {
        reset();
        reader.fail();
        return;
    }
    address_ = address;
    data_ = data;
}

} // namespace mapperbus::core
