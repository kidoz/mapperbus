#include "core/mappers/mapper112.hpp"

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr uint16_t kStateTag = 0x7000U;
} // namespace

// Register behavior: NESdev mapper 112 r23867. Fixed PRG and paired CHR windows
// follow its linked mapper-206 r23830 reference. Implementation is original.
Mapper112::Mapper112(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    reset();
}

Result<void> Mapper112::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 112 supports submapper 0");
    if (prg_size < 0x8000U || prg_size > 0x200000U || prg_size % 0x2000U != 0U ||
        chr_size < 0x2000U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
        return std::unexpected(
            "Mapper 112 requires 32 KiB-2 MiB PRG and 8-256 KiB CHR ROM in bank units");
    if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
        return std::unexpected("Mapper 112 supports ROM-only board memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 112 supports ordinary H/V CIRAM");
    return {};
}

Byte Mapper112::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty())
        return 0U;
    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    const std::size_t bank = slot < 2U ? banks_[slot] : prg_rom_.size() / 0x2000U - 4U + slot;
    return prg_rom_[(bank * 0x2000U + (addr & 0x1FFFU)) % prg_rom_.size()];
}

void Mapper112::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    switch (addr & 0xE001U) {
    case 0x8000U:
        select_ = value & 7U;
        break;
    case 0xA000U:
        banks_[select_] = value;
        break;
    case 0xE000U:
        mirror_ = (value & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
        break;
    default:
        break;
    }
}

Byte Mapper112::read_chr(Address addr) {
    if (chr_rom_.empty())
        return 0U;
    addr &= 0x1FFFU;
    std::size_t bank = 0U;
    if (addr < 0x1000U) {
        const auto reg = static_cast<unsigned>(2U + (addr >> 11U));
        bank = (banks_[reg] & 0xFEU) | ((addr >> 10U) & 1U);
    } else {
        bank = banks_[4U + ((addr - 0x1000U) >> 10U)];
    }
    return chr_rom_[(bank * 0x400U + (addr & 0x3FFU)) % chr_rom_.size()];
}

void Mapper112::write_chr(Address, Byte) {}

MirrorMode Mapper112::mirror_mode() const {
    return mirror_;
}

void Mapper112::reset() {
    select_ = 0U;
    banks_.fill(0U);
    mirror_ = initial_mirror_;
}

uint16_t Mapper112::state_tag() const {
    return static_cast<uint16_t>(kStateTag | (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper112::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(select_);
    writer.write_array(banks_);
    writer.write<Byte>(static_cast<Byte>(mirror_));
}

void Mapper112::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto select = reader.read<Byte>();
    std::array<Byte, 8> banks{};
    reader.read_array(banks);
    const auto mirror = reader.read<Byte>();
    if (!reader.ok() || tag != state_tag() || select > 7U ||
        mirror > static_cast<Byte>(MirrorMode::Vertical)) {
        reader.fail();
        return;
    }
    select_ = select;
    banks_ = banks;
    mirror_ = static_cast<MirrorMode>(mirror);
}

} // namespace mapperbus::core
