#include "core/mappers/mapper286_benshieng.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t kPrgBankSize = 0x2000U;
constexpr std::size_t kChrBankSize = 0x0800U;
constexpr uint16_t kStateTag = 0xA286U;
} // namespace

// Original decoding from NESdev mapper 286 r24644. CHR bank zero and horizontal
// CIRAM on reset are deterministic policies; the article specifies only PRG=$0F.
BenshiengBs5::BenshiengBs5(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> BenshiengBs5::validate_geometry(const INesHeader& header,
                                             std::size_t prg_size,
                                             std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 286 supports submapper 0");
    if (prg_size < 0x8000U || prg_size > 0x20000U || prg_size % kPrgBankSize != 0U ||
        chr_size < 0x2000U || chr_size > 0x8000U || chr_size % kChrBankSize != 0U)
        return std::unexpected(
            "Mapper 286 requires 32-128 KiB PRG and 8-32 KiB CHR ROM in native bank units");
    if (header.has_battery ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
                            header.chr_ram_size != 0U || header.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 286 supports ROM-only board memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 286 supports ordinary H/V CIRAM metadata");
    return {};
}

Byte BenshiengBs5::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    const auto slot = static_cast<std::size_t>((addr >> 13U) & 3U);
    const auto bank_count = prg_rom_.size() / kPrgBankSize;
    const auto bank = static_cast<std::size_t>(prg_banks_[slot]) % bank_count;
    return prg_rom_[bank * kPrgBankSize + (addr & 0x1FFFU)];
}

void BenshiengBs5::write_prg(Address addr, Byte /*value*/) {
    if (addr < 0x8000U || addr >= 0xE000U)
        return;
    const auto slot = static_cast<std::size_t>((addr >> 8U) & 3U);
    if (addr < 0xA000U) {
        chr_banks_[slot] = static_cast<Byte>(addr & 0x0FU);
    } else if (addr < 0xC000U) {
        const Byte address_mask = static_cast<Byte>((addr >> 4U) & 0x0FU);
        const Byte active_mask = static_cast<Byte>(address_mask | (~pads_ & 0x0FU));
        prg_banks_[slot] = static_cast<Byte>((addr & 0x0FU) & active_mask);
    } else {
        mirror_ = (addr & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    }
}

Byte BenshiengBs5::read_chr(Address addr) {
    const auto slot = static_cast<std::size_t>((addr >> 11U) & 3U);
    const auto bank_count = chr_rom_.size() / kChrBankSize;
    const auto bank = static_cast<std::size_t>(chr_banks_[slot]) % bank_count;
    return chr_rom_[bank * kChrBankSize + (addr & 0x07FFU)];
}

void BenshiengBs5::write_chr(Address, Byte) {}

MirrorMode BenshiengBs5::mirror_mode() const {
    return mirror_;
}

Result<void> BenshiengBs5::configure_board_jumpers(Byte value) {
    if (value > 15U)
        return std::unexpected("Mapper 286 PRG-mask solder pads require a value 0..15");
    pads_ = value;
    return {};
}

Result<void> BenshiengBs5::configure_board_profile(std::string_view profile) {
    if (!profile.starts_with("mask-"))
        return std::unexpected("Mapper 286 board profiles are mask-0 through mask-15");
    profile.remove_prefix(5U);
    unsigned value = 0U;
    const auto [end, error] =
        std::from_chars(profile.data(), profile.data() + profile.size(), value);
    if (error != std::errc{} || end != profile.data() + profile.size() || value > 15U)
        return std::unexpected("Mapper 286 board profiles are mask-0 through mask-15");
    return configure_board_jumpers(static_cast<Byte>(value));
}

void BenshiengBs5::reset() {
    prg_banks_.fill(0x0FU);
    chr_banks_.fill(0U);
    mirror_ = MirrorMode::Horizontal;
}

void BenshiengBs5::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(pads_);
    writer.write(static_cast<Byte>(mirror_ == MirrorMode::Vertical));
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write(static_cast<uint32_t>(prg_rom_.size()));
    writer.write(static_cast<uint32_t>(chr_rom_.size()));
}

void BenshiengBs5::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto pads = reader.read<Byte>();
    const auto mirror = reader.read<Byte>();
    std::array<Byte, 4> prg_banks{};
    std::array<Byte, 4> chr_banks{};
    reader.read_array(prg_banks);
    reader.read_array(chr_banks);
    const auto prg_size = reader.read<uint32_t>();
    const auto chr_size = reader.read<uint32_t>();
    const auto valid_bank = [](Byte bank) { return bank <= 0x0FU; };
    if (!reader.ok() || tag != kStateTag || pads != pads_ || mirror > 1U ||
        !std::ranges::all_of(prg_banks, valid_bank) ||
        !std::ranges::all_of(chr_banks, valid_bank) || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size()) {
        reader.fail();
        return;
    }
    prg_banks_ = prg_banks;
    chr_banks_ = chr_banks;
    mirror_ = mirror != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
}

} // namespace mapperbus::core
