#include "core/mappers/fds_conversion108.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

Byte select_submapper(const INesHeader& header, std::size_t chr_size) {
    if (header.is_nes2)
        return header.submapper;
    if (chr_size == 0U)
        return header.mirror_mode == MirrorMode::Horizontal ? 1U : 3U;
    return chr_size > 16384U ? 2U : 4U;
}

} // namespace

// Original implementation from NESdev mapper 108 revision 21237. Reset and
// power-on values are deterministic policies; the article does not specify them.
FdsConversion108::FdsConversion108(const INesHeader& header,
                                   std::vector<Byte> prg_rom,
                                   std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_(std::move(chr_rom)), mirror_(header.mirror_mode),
      submapper_(select_submapper(header, chr_.size())), has_chr_ram_(chr_.empty()) {
    power_on();
}

Result<void> FdsConversion108::validate_geometry(const INesHeader& header,
                                                 std::size_t prg_size,
                                                 std::size_t chr_size) {
    if (prg_size < 32768U || prg_size > 2097152U || !std::has_single_bit(prg_size))
        return std::unexpected("Mapper 108 requires power-of-two 32 KiB-2 MiB PRG ROM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 108 requires fixed H/V mirroring");
    if (header.has_battery)
        return std::unexpected("Mapper 108 has no battery-backed memory");

    const auto submapper = select_submapper(header, chr_size);
    if (header.is_nes2 && (submapper < 1U || submapper > 4U))
        return std::unexpected("Mapper 108 requires NES 2.0 submapper 1-4");
    if ((submapper == 1U || submapper == 3U) && chr_size != 0U)
        return std::unexpected("Mapper 108 submappers 1/3 require 8 KiB CHR RAM");
    if (submapper == 2U &&
        (chr_size <= 16384U || chr_size > 2097152U || !std::has_single_bit(chr_size)))
        return std::unexpected("Mapper 108 submapper 2 requires power-of-two 32 KiB-2 MiB CHR ROM");
    if (submapper == 4U && chr_size != 8192U && chr_size != 16384U)
        return std::unexpected("Mapper 108 submapper 4 requires 8/16 KiB CHR ROM");
    if ((submapper == 1U && header.mirror_mode != MirrorMode::Horizontal) ||
        (submapper == 3U && header.mirror_mode != MirrorMode::Vertical))
        return std::unexpected("Mapper 108 CHR-RAM boards use the documented fixed mirroring");

    if (header.is_nes2 &&
        (header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
         header.chr_ram_size != (chr_size == 0U ? 8192U : 0U) || header.chr_nvram_size != 0U))
        return std::unexpected("Mapper 108 requires no CPU RAM and only optional 8 KiB CHR RAM");
    return {};
}

Byte FdsConversion108::read_prg(Address addr) {
    if (addr >= 0x8000U)
        return prg_rom_[prg_rom_.size() - 32768U + (addr & 0x7FFFU)];
    if (addr < 0x6000U)
        return 0U;
    if (submapper_ == 4U)
        return prg_rom_[prg_rom_.size() - 8192U + (addr & 0x1FFFU)];
    const auto bank = static_cast<std::size_t>(prg_bank_) & (prg_rom_.size() / 8192U - 1U);
    return prg_rom_[bank * 8192U + (addr & 0x1FFFU)];
}

void FdsConversion108::write_prg(Address addr, Byte value) {
    switch (submapper_) {
    case 1U:
        if (addr >= 0xF000U)
            prg_bank_ = value;
        break;
    case 2U:
        if (addr >= 0xE000U) {
            prg_bank_ = value;
            chr_bank_ = value;
        }
        break;
    case 3U:
        if (addr >= 0x8000U)
            prg_bank_ = value;
        break;
    case 4U:
        if (addr >= 0x8000U)
            chr_bank_ = value;
        break;
    default:
        break;
    }
}

bool FdsConversion108::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte FdsConversion108::read_chr(Address addr) {
    const auto offset = addr & 0x1FFFU;
    if (has_chr_ram_)
        return chr_ram_[offset];
    const auto bank = static_cast<std::size_t>(chr_bank_) & (chr_.size() / 8192U - 1U);
    return chr_[bank * 8192U + offset];
}

void FdsConversion108::write_chr(Address addr, Byte value) {
    if (has_chr_ram_)
        chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode FdsConversion108::mirror_mode() const {
    return mirror_;
}

void FdsConversion108::reset() {
    prg_bank_ = 0U;
    chr_bank_ = 0U;
}

void FdsConversion108::power_on() {
    reset();
    if (has_chr_ram_)
        chr_ram_.fill(0U);
}

uint16_t FdsConversion108::state_tag() const {
    return static_cast<uint16_t>(0x6C00U | submapper_ |
                                 (mirror_ == MirrorMode::Vertical ? 0x10U : 0U) |
                                 (has_chr_ram_ ? 0x20U : 0U));
}

uint32_t FdsConversion108::saved_chr_size() const {
    return has_chr_ram_ ? 0U : static_cast<uint32_t>(chr_.size());
}

void FdsConversion108::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<uint32_t>(prg_rom_.size()));
    writer.write(saved_chr_size());
    writer.write(prg_bank_);
    writer.write(chr_bank_);
    if (has_chr_ram_)
        writer.write_array(chr_ram_);
}

void FdsConversion108::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_size = reader.read<uint32_t>();
    const auto chr_size = reader.read<uint32_t>();
    const auto prg_bank = reader.read<Byte>();
    const auto chr_bank = reader.read<Byte>();
    std::array<Byte, 8192> chr_ram{};
    if (has_chr_ram_)
        reader.read_array(chr_ram);
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != saved_chr_size() ||
        ((submapper_ == 1U || submapper_ == 3U) && chr_bank != 0U) ||
        (submapper_ == 4U && prg_bank != 0U)) {
        reader.fail();
        return;
    }
    prg_bank_ = prg_bank;
    chr_bank_ = chr_bank;
    if (has_chr_ram_)
        chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
