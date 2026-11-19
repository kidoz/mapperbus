#include "core/mappers/cartridge_story_274.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t kMainRomSize = 0x200000U;
constexpr uint16_t kStateTag = 0x1200U;
} // namespace

CartridgeStory274::CartridgeStory274(const INesHeader&,
                                     std::vector<Byte> prg_rom,
                                     const std::vector<Byte>&)
    : prg_rom_(std::move(prg_rom)), extra_size_(prg_rom_.size() - kMainRomSize) {}

Result<void> CartridgeStory274::validate_geometry(const INesHeader& header,
                                                  std::size_t prg_size,
                                                  std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 274 supports submapper 0");
    if (prg_size != 0x200000U && prg_size != 0x204000U && prg_size != 0x210000U)
        return std::unexpected("Mapper 274 requires 2 MiB main PRG and 0/16/64 KiB extra PRG");
    if (chr_size != 0U)
        return std::unexpected("Mapper 274 requires CHR RAM instead of CHR ROM");
    if (header.has_battery || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
                            header.chr_ram_size != 8192U)))
        return std::unexpected("Mapper 274 requires 8 KiB volatile CHR RAM and no other RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 274 supports ordinary H/V CIRAM");
    return {};
}

Byte CartridgeStory274::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    const auto offset = static_cast<std::size_t>(addr & 0x3FFFU);
    if (addr >= 0xC000U)
        return prg_rom_[static_cast<std::size_t>(outer_bank_ & 0x7FU) * 0x4000U + offset];
    if ((outer_bank_ & 0x80U) == 0U) {
        if (extra_size_ == 0U)
            return 0U;
        const auto page =
            static_cast<std::size_t>(inner_bank_ & 0x0FU) & ((extra_size_ / 0x4000U) - 1U);
        return prg_rom_[kMainRomSize + page * 0x4000U + offset];
    }
    const auto page = static_cast<std::size_t>((outer_bank_ & 0x70U) | (inner_bank_ & 0x0FU));
    return prg_rom_[page * 0x4000U + offset];
}

void CartridgeStory274::write_prg(Address addr, Byte value) {
    // The pinned address diagram fixes A14 for the outer latch; A13 selects the chip.
    if ((addr & 0xE000U) == 0x8000U)
        inner_bank_ = value & 0x1FU;
    else if (addr >= 0xC000U)
        outer_bank_ =
            static_cast<Byte>((value & 0x7FU) | ((static_cast<unsigned>(addr) >> 6U) & 0x80U));
}

Byte CartridgeStory274::prg_read_mask(Address addr) const {
    return addr >= 0x8000U && addr < 0xC000U && (outer_bank_ & 0x80U) == 0U && extra_size_ == 0U
               ? 0U
               : 0xFFU;
}

Byte CartridgeStory274::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void CartridgeStory274::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode CartridgeStory274::mirror_mode() const {
    return (inner_bank_ & 0x10U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void CartridgeStory274::reset() {
    // Reset chip selection is specified; clearing the other fields is deterministic policy.
    inner_bank_ = 0U;
    outer_bank_ = 0U;
}

void CartridgeStory274::power_on() {
    reset();
    chr_ram_.fill(0U);
}

uint16_t CartridgeStory274::state_tag() const {
    const auto population = extra_size_ == 0U ? 0U : extra_size_ == 0x4000U ? 1U : 2U;
    return static_cast<uint16_t>(kStateTag | population);
}

void CartridgeStory274::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(inner_bank_);
    writer.write(outer_bank_);
    writer.write_array(chr_ram_);
}

void CartridgeStory274::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto inner = reader.read<Byte>();
    const auto outer = reader.read<Byte>();
    std::array<Byte, 8192> chr{};
    reader.read_array(chr);
    if (!reader.ok() || tag != state_tag() || (inner & 0xE0U) != 0U) {
        reader.fail();
        return;
    }
    inner_bank_ = inner;
    outer_bank_ = outer;
    chr_ram_ = chr;
}

} // namespace mapperbus::core
