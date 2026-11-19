#include "core/mappers/mapper301.hpp"

#include <algorithm>
#include <charconv>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t kPrgPageSize = 16U * 1024U;
constexpr std::size_t kMainRomSize = 512U * 1024U;
constexpr std::size_t kEt113RomSize = 640U * 1024U;
constexpr std::size_t kCartRamSize = 8U * 1024U;
constexpr Address kLatchMask = 0x07FFU;
constexpr Address kSelect32K = 0x0001U;
constexpr Address kHorizontal = 0x0002U;
constexpr Address kNromMode = 0x0080U;
constexpr Address kUseSolderPads = 0x0100U;
constexpr Address kFixedLastPage = 0x0200U;
constexpr Address kSelectMainRom = 0x0400U;
constexpr uint16_t kStateBase = 0xD300U;
} // namespace

// Original implementation of the address-pin description in NESdev r19709.
Mapper301::Mapper301(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), wram_(header.has_battery ? kCartRamSize : 0U) {}

Result<void> Mapper301::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 301 supports only submapper 0");
    const bool main_rom = prg_size == kMainRomSize;
    const bool et113_rom = prg_size == kEt113RomSize && !header.has_battery;
    if (chr_size != 0U || (!main_rom && !et113_rom))
        return std::unexpected(
            "Mapper 301 requires 512 KiB PRG, or battery-free 640 KiB ET-113, and no CHR ROM");
    if (header.is_nes2 && (header.prg_ram_size != 0U ||
                           header.prg_nvram_size != (header.has_battery ? kCartRamSize : 0U) ||
                           header.chr_ram_size != kCartRamSize || header.chr_nvram_size != 0U))
        return std::unexpected(
            "Mapper 301 requires 8 KiB CHR RAM and only optional 8 KiB battery PRG NVRAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 301 supports horizontal or vertical CIRAM metadata");
    return {};
}

Byte Mapper301::read_prg(Address addr) {
    if (addr < 0x8000U) {
        if (addr < 0x6000U || !has_wram())
            return 0U;
        return wram_[addr & 0x1FFFU];
    }

    const bool upper_window = (addr & 0x4000U) != 0U;
    unsigned page = (latch_ >> 2U) & 0x07U;
    if ((latch_ & kSelect32K) != 0U) {
        page = (page & 0x06U) | static_cast<unsigned>(upper_window);
    } else if (upper_window && !has_wram() && (latch_ & kNromMode) == 0U) {
        page = (latch_ & kFixedLastPage) != 0U ? 7U : 0U;
    }

    const unsigned outer_page = (latch_ >> 5U) & 0x03U;
    std::size_t offset = 0U;
    if (is_et113() && (latch_ & kSelectMainRom) == 0U) {
        offset = kMainRomSize + page * kPrgPageSize;
    } else {
        offset = (outer_page * 8U + page) * kPrgPageSize;
    }
    offset += addr & 0x3FFFU;
    if ((latch_ & kUseSolderPads) != 0U)
        offset = (offset & ~0x1FU) | solder_pads_;
    return prg_rom_[offset];
}

void Mapper301::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U) {
        latch_ = addr & kLatchMask;
    } else if (addr >= 0x6000U && has_wram()) {
        wram_[addr & 0x1FFFU] = value;
    }
}

bool Mapper301::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && has_wram());
}

Byte Mapper301::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper301::write_chr(Address addr, Byte value) {
    if (has_wram() || (latch_ & kNromMode) == 0U)
        chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper301::mirror_mode() const {
    return (latch_ & kHorizontal) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

Result<void> Mapper301::configure_board_jumpers(Byte value) {
    if (value >= 32U)
        return std::unexpected("Mapper 301 solder pads require a value from 0 through 31");
    solder_pads_ = value;
    return {};
}

Result<void> Mapper301::configure_board_profile(std::string_view profile) {
    if (!profile.starts_with("menu-"))
        return std::unexpected("Mapper 301 profiles are menu-0 through menu-31");
    profile.remove_prefix(5U);
    unsigned value = 0U;
    const auto* const end = profile.data() + profile.size();
    const auto [parsed_end, error] = std::from_chars(profile.data(), end, value);
    if (error != std::errc{} || parsed_end != end || value >= 32U)
        return std::unexpected("Mapper 301 profiles are menu-0 through menu-31");
    return configure_board_jumpers(static_cast<Byte>(value));
}

std::span<const Byte> Mapper301::battery_ram() const {
    return wram_;
}

void Mapper301::set_battery_ram(std::span<const Byte> data) {
    const auto count = std::min(data.size(), wram_.size());
    std::copy_n(data.begin(), count, wram_.begin());
}

void Mapper301::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper301::reset() {
    latch_ = 0U;
}

bool Mapper301::is_et113() const {
    return prg_rom_.size() == kEt113RomSize;
}

bool Mapper301::has_wram() const {
    return !wram_.empty();
}

uint16_t Mapper301::state_tag() const {
    return static_cast<uint16_t>(kStateBase | (static_cast<uint16_t>(solder_pads_) << 2U) |
                                 (has_wram() ? 1U : 0U) | (is_et113() ? 2U : 0U));
}

void Mapper301::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(latch_);
    writer.write_array(chr_ram_);
    if (has_wram()) {
        std::array<Byte, kCartRamSize> saved_wram{};
        std::ranges::copy(wram_, saved_wram.begin());
        writer.write_array(saved_wram);
    }
}

void Mapper301::load_state(StateReader& reader) {
    const auto saved_tag = reader.read<uint16_t>();
    const auto saved_latch = reader.read<Address>();
    std::array<Byte, kCartRamSize> saved_chr{};
    std::array<Byte, kCartRamSize> saved_wram{};
    reader.read_array(saved_chr);
    if (has_wram())
        reader.read_array(saved_wram);

    if (!reader.ok() || saved_tag != state_tag() || saved_latch > kLatchMask) {
        reader.fail();
        return;
    }

    latch_ = saved_latch;
    chr_ram_ = saved_chr;
    if (has_wram())
        std::ranges::copy(saved_wram, wram_.begin());
}

} // namespace mapperbus::core
