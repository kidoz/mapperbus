#include "core/mappers/mapper381.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x3810U;
constexpr Byte kInnerBankMask = 0x0FU;

} // namespace

// Original implementation from NESdev mapper 381 oldid 24543. Console reset
// models the documented M2 interruption and flips only PRG A18. Cold zeroing
// of the data latch and CHR RAM is a deterministic policy.
Mapper381::Mapper381(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper381::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 381U) {
        return std::unexpected("Mapper 381 requires mapper ID 381");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 381 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != 0U) {
        return std::unexpected("Mapper 381 requires 512 KiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 381 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 381 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper381::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }

    const Byte inner_bank = (addr & 0x4000U) != 0U ? kInnerBankMask : inner_bank_;
    const auto bank = static_cast<std::size_t>((outer_bank_ << 4U) | inner_bank);
    const auto offset = bank * kPrgBankSize + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper381::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }

    const Byte latched = value & read_prg(addr);
    inner_bank_ = static_cast<Byte>(((latched & 0x07U) << 1U) | ((latched >> 4U) & 1U));
}

bool Mapper381::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper381::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper381::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper381::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper381::mirror_mode() const {
    return initial_mirror_;
}

void Mapper381::power_on() {
    inner_bank_ = 0U;
    outer_bank_ = 0U;
    chr_ram_.fill(0U);
}

void Mapper381::reset() {
    outer_bank_ ^= 1U;
}

std::uint16_t Mapper381::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper381::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(inner_bank_);
    writer.write(outer_bank_);
    writer.write_array(chr_ram_);
}

void Mapper381::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto inner_bank = reader.read<Byte>();
    const auto outer_bank = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() || chr_size != 0U ||
        inner_bank > kInnerBankMask || outer_bank > 1U) {
        reader.fail();
        return;
    }

    inner_bank_ = inner_bank;
    outer_bank_ = outer_bank;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
