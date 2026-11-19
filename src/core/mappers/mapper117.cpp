#include "core/mappers/mapper117.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr uint16_t kStateTag = 0x7500U;
constexpr uint16_t kVisibleScanlines = 240U;
constexpr uint16_t kIrqScanlineBias = 0x14U;

} // namespace

// Original implementation from NESdev mapper 117 r4888. The article's uncertain
// scanline-minus-$14 note is modeled as a write value biased by +$14 relative
// to the visible PPU line; its direct-comparison rule is not an MMC3 countdown.
Mapper117::Mapper117(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), region_(header.region),
      frame_scanlines_(static_cast<uint16_t>(timing_for_region(region_).scanlines_per_frame)) {
    reset();
}

Result<void> Mapper117::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 117U)
        return std::unexpected("Mapper 117 requires mapper ID 117");
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 117 supports submapper 0");
    if (prg_size < 0x8000U || prg_size > 0x200000U || prg_size % 0x2000U != 0U ||
        chr_size < 0x2000U || chr_size > 0x40000U || chr_size % 0x400U != 0U)
        return std::unexpected(
            "Mapper 117 requires 32 KiB-2 MiB PRG and 8-256 KiB CHR ROM in bank units");
    if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
        return std::unexpected("Mapper 117 supports ROM-only board memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 117 supports fixed H/V CIRAM");
    return {};
}

Byte Mapper117::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    const std::size_t bank = slot == 3U ? prg_rom_.size() / 0x2000U - 1U : prg_banks_[slot];
    return prg_rom_[(bank * 0x2000U + (addr & 0x1FFFU)) % prg_rom_.size()];
}

void Mapper117::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U && addr <= 0x8002U) {
        prg_banks_[addr - 0x8000U] = value;
    } else if (addr >= 0xA000U && addr <= 0xA007U) {
        chr_banks_[addr - 0xA000U] = value;
    } else if (addr >= 0xC000U && addr <= 0xC002U) {
        irq_counter_ = value;
        irq_latch_ = value;
        irq_pending_ = false;
    } else if (addr == 0xE000U) {
        irq_enabled_ = (value & 1U) != 0U;
    }
}

Byte Mapper117::read_chr(Address addr) {
    addr &= 0x1FFFU;
    const auto bank = chr_banks_[addr >> 10U];
    return chr_rom_[(static_cast<std::size_t>(bank) * 0x400U + (addr & 0x3FFU)) % chr_rom_.size()];
}

void Mapper117::write_chr(Address, Byte) {}

MirrorMode Mapper117::mirror_mode() const {
    return initial_mirror_;
}

void Mapper117::set_region(Region region) {
    region_ = region;
    frame_scanlines_ = static_cast<uint16_t>(timing_for_region(region_).scanlines_per_frame);
    scanline_ = static_cast<uint16_t>(scanline_ % frame_scanlines_);
}

void Mapper117::reset() {
    prg_banks_.fill(0U);
    chr_banks_.fill(0U);
    irq_counter_ = 0U;
    irq_latch_ = 0U;
    irq_enabled_ = false;
    irq_pending_ = false;
    scanline_ = 0U;
}

bool Mapper117::irq_pending() const {
    return irq_pending_;
}

void Mapper117::on_ppu_hblank() {
    const auto irq_scanline = static_cast<uint16_t>(irq_counter_ + kIrqScanlineBias);
    if (scanline_ < kVisibleScanlines && irq_enabled_ && irq_latch_ != 0U &&
        scanline_ == irq_scanline) {
        irq_pending_ = true;
        irq_counter_ = 0U;
    }
    ++scanline_;
    if (scanline_ == frame_scanlines_)
        scanline_ = 0U;
}

uint16_t Mapper117::state_tag() const {
    return static_cast<uint16_t>(kStateTag | (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper117::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write<uint32_t>(static_cast<uint32_t>(prg_rom_.size()));
    writer.write<uint32_t>(static_cast<uint32_t>(chr_rom_.size()));
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write(irq_counter_);
    writer.write(irq_latch_);
    writer.write<Byte>((irq_enabled_ ? 1U : 0U) | (irq_pending_ ? 2U : 0U));
    writer.write(scanline_);
    writer.write<Byte>(static_cast<Byte>(region_));
}

void Mapper117::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_size = reader.read<uint32_t>();
    const auto chr_size = reader.read<uint32_t>();
    std::array<Byte, 3> prg_banks{};
    std::array<Byte, 8> chr_banks{};
    reader.read_array(prg_banks);
    reader.read_array(chr_banks);
    const auto irq_counter = reader.read<Byte>();
    const auto irq_latch = reader.read<Byte>();
    const auto irq_flags = reader.read<Byte>();
    const auto scanline = reader.read<uint16_t>();
    const auto region = reader.read<Byte>();

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || irq_flags > 3U || scanline >= frame_scanlines_ ||
        region != static_cast<Byte>(region_)) {
        reader.fail();
        return;
    }

    prg_banks_ = prg_banks;
    chr_banks_ = chr_banks;
    irq_counter_ = irq_counter;
    irq_latch_ = irq_latch;
    irq_enabled_ = (irq_flags & 1U) != 0U;
    irq_pending_ = (irq_flags & 2U) != 0U;
    scanline_ = scanline;
}

} // namespace mapperbus::core
