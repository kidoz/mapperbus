#include "core/mappers/karaoke_studio_188.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::uint16_t kStateTag = 0xBC00U;
} // namespace

KaraokeStudio188::KaraokeStudio188(const INesHeader&,
                                   std::vector<Byte> prg_rom,
                                   std::vector<Byte>) {
    const std::size_t internal_size =
        prg_rom.size() <= 0x20000U ? prg_rom.size() : prg_rom.size() / 2U;
    const auto split = prg_rom.begin() + static_cast<std::ptrdiff_t>(internal_size);
    internal_rom_.assign(prg_rom.begin(), split);
    external_rom_.assign(split, prg_rom.end());
    power_on();
}

Result<void> KaraokeStudio188::validate_geometry(const INesHeader& header,
                                                 std::size_t prg_size,
                                                 std::size_t chr_size) {
    if (header.mapper_number != 188U || (header.is_nes2 && header.submapper != 0U))
        return std::unexpected("Mapper 188 supports only mapper 188 submapper 0");
    const bool internal_only =
        prg_size >= 0x4000U && prg_size <= 0x20000U && std::has_single_bit(prg_size);
    const bool packed_pair = prg_size == 0x40000U || prg_size == 0x80000U;
    if (!internal_only && !packed_pair)
        return std::unexpected("Mapper 188 requires 16-128 KiB internal PRG ROM or a packed "
                               "256/512 KiB internal/expansion pair");
    if (chr_size != 0U)
        return std::unexpected("Mapper 188 requires 8 KiB CHR RAM and no CHR ROM");
    if (header.has_battery || header.prg_nvram_size != 0U || header.chr_ram_size != 0x2000U ||
        header.chr_nvram_size != 0U || (header.is_nes2 && header.prg_ram_size != 0U))
        return std::unexpected("Mapper 188 requires only 8 KiB volatile CHR RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 188 supports ordinary H/V CIRAM metadata");
    return {};
}

void KaraokeStudio188::set_microphone_inputs(bool level,
                                             bool button_a_pressed,
                                             bool button_b_pressed) {
    microphone_level_ = level;
    button_a_pressed_ = button_a_pressed;
    button_b_pressed_ = button_b_pressed;
}

std::span<const Byte> KaraokeStudio188::selected_lower_rom() const {
    return (control_ & 0x10U) != 0U ? std::span<const Byte>(internal_rom_)
                                    : std::span<const Byte>(external_rom_);
}

Byte KaraokeStudio188::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        return static_cast<Byte>((microphone_level_ ? 0x04U : 0U) |
                                 (button_b_pressed_ ? 0U : 0x02U) |
                                 (button_a_pressed_ ? 0U : 0x01U));
    }
    if (addr < 0x8000U)
        return 0U;
    if (addr >= 0xC000U) {
        const std::size_t offset = internal_rom_.size() - 0x4000U + (addr & 0x3FFFU);
        return internal_rom_[offset];
    }
    const auto rom = selected_lower_rom();
    if (rom.empty())
        return 0U;
    const std::size_t bank_count = rom.size() / 0x4000U;
    const std::size_t bank = (control_ & 0x0FU) % bank_count;
    return rom[bank * 0x4000U + (addr & 0x3FFFU)];
}

void KaraokeStudio188::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    const auto rom = addr >= 0xC000U ? std::span<const Byte>(internal_rom_) : selected_lower_rom();
    const Byte driven = rom.empty() ? 0xFFU : read_prg(addr);
    control_ = value & driven & 0x7FU;
}

bool KaraokeStudio188::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte KaraokeStudio188::prg_read_mask(Address addr) const {
    if (addr >= 0x6000U && addr < 0x8000U)
        return 0x07U;
    if (addr < 0x8000U)
        return 0U;
    if (addr < 0xC000U && selected_lower_rom().empty())
        return 0U;
    return 0xFFU;
}

Byte KaraokeStudio188::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void KaraokeStudio188::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode KaraokeStudio188::mirror_mode() const {
    return (control_ & 0x20U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void KaraokeStudio188::power_on() {
    control_ = 0U;
    chr_ram_.fill(0U);
}

void KaraokeStudio188::reset() {
    // The M60001 latch has no reset input; CHR RAM and live inputs also survive.
}

void KaraokeStudio188::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(internal_rom_.size()));
    writer.write(static_cast<std::uint32_t>(external_rom_.size()));
    writer.write(control_);
    writer.write_array(chr_ram_);
}

void KaraokeStudio188::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto internal_size = reader.read<std::uint32_t>();
    const auto external_size = reader.read<std::uint32_t>();
    const auto control = reader.read<Byte>();
    std::array<Byte, 0x2000> chr_ram{};
    reader.read_array(chr_ram);
    if (!reader.ok() || tag != kStateTag || internal_size != internal_rom_.size() ||
        external_size != external_rom_.size() || (control & 0x80U) != 0U) {
        reader.fail();
        return;
    }
    control_ = control;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
