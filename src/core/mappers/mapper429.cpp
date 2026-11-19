#include "core/mappers/mapper429.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kDiyPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x008000UZ;
constexpr std::size_t kChrBankSize = 0x002000UZ;
constexpr std::uint16_t kStateBase = 0x4290U;

bool supported_mirror(MirrorMode mirror) {
    return mirror == MirrorMode::Horizontal || mirror == MirrorMode::Vertical ||
           mirror == MirrorMode::SingleLower || mirror == MirrorMode::SingleUpper;
}

Byte register_mask(Byte submapper) {
    return submapper == 1U ? 0xFFU : submapper == 2U ? 0x0FU : 0x7FU;
}

} // namespace

// Original implementation from NES 2.0 mapper 429 oldid 23470. Soft reset restores
// the documented bank defaults and retains RAM; cold power clears only volatile RAM.
Mapper429::Mapper429(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), initial_mirror_(header.mirror_mode),
      submapper_(header.submapper), persistent_chr_(header.chr_nvram_size == kChrRamSize) {
    chr_rom.clear();
    power_on();
}

Result<void> Mapper429::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 429U) {
        return std::unexpected("Mapper 429 requires mapper ID 429");
    }
    if (!header.is_nes2 || header.submapper > 2U) {
        return std::unexpected("Mapper 429 requires NES 2.0 submapper 0, 1 or 2");
    }
    if ((prg_size != kPrgSize && prg_size != kDiyPrgSize) || chr_size != 0U) {
        return std::unexpected("Mapper 429 requires 512 KiB or 1 MiB PRG ROM and no CHR ROM");
    }
    const bool volatile_chr =
        header.chr_ram_size == kChrRamSize && header.chr_nvram_size == 0U && !header.has_battery;
    const bool persistent_chr =
        header.chr_ram_size == 0U && header.chr_nvram_size == kChrRamSize && header.has_battery;
    if (header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        (!volatile_chr && !persistent_chr)) {
        return std::unexpected(
            "Mapper 429 requires only 32 KiB volatile CHR RAM or battery-backed CHR NVRAM");
    }
    if (!supported_mirror(header.mirror_mode)) {
        return std::unexpected("Mapper 429 requires fixed H/V or single-screen CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 429 does not provide trainer RAM");
    }
    return {};
}

bool Mapper429::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper429::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper429::read_prg(Address addr) {
    if (!maps_prg(addr) || prg_rom_.empty()) {
        return 0U;
    }
    const auto bank_mask = submapper_ == 2U ? 0x03U : 0x1FU;
    const auto bank = static_cast<std::size_t>((latch_ >> 2U) & bank_mask);
    const auto offset = bank * kPrgBankSize + (addr & 0x7FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper429::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
    if (submapper_ == 2U) {
        value &= read_prg(addr);
    }
    latch_ = value & register_mask(submapper_);
}

unsigned Mapper429::chr_bank() const {
    unsigned bank = latch_ & 3U;
    if (submapper_ == 2U && (latch_ & 0x0CU) == 0x0CU) {
        bank |= 4U;
    }
    return bank;
}

Byte Mapper429::read_chr(Address addr) {
    const auto offset = chr_bank() * kChrBankSize + (addr & 0x1FFFU);
    return chr_ram_[offset % chr_ram_.size()];
}

void Mapper429::write_chr(Address addr, Byte value) {
    const auto offset = chr_bank() * kChrBankSize + (addr & 0x1FFFU);
    chr_ram_[offset % chr_ram_.size()] = value;
}

MirrorMode Mapper429::mirror_mode() const {
    if (submapper_ == 1U) {
        return (latch_ & 0x80U) != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    }
    return initial_mirror_;
}

std::span<const Byte> Mapper429::battery_ram() const {
    return persistent_chr_ ? std::span<const Byte>{chr_ram_} : std::span<const Byte>{};
}

void Mapper429::set_battery_ram(std::span<const Byte> data) {
    if (persistent_chr_ && data.size() == chr_ram_.size()) {
        std::ranges::copy(data, chr_ram_.begin());
    }
}

void Mapper429::power_on() {
    reset();
    if (!persistent_chr_) {
        chr_ram_.fill(0U);
    }
}

void Mapper429::reset() {
    latch_ = submapper_ == 2U ? 0U : 0x04U;
}

std::uint16_t Mapper429::state_tag() const {
    return static_cast<std::uint16_t>(kStateBase | submapper_ | (persistent_chr_ ? 4U : 0U) |
                                      (static_cast<std::uint16_t>(initial_mirror_) << 3U));
}

void Mapper429::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write<std::uint32_t>(persistent_chr_ ? kChrRamSize : 0U);
    writer.write(latch_);
    writer.write_array(chr_ram_);
}

void Mapper429::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto nvram_size = reader.read<std::uint32_t>();
    const auto latch = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() || chr_size != 0U ||
        nvram_size != (persistent_chr_ ? kChrRamSize : 0U) ||
        (latch & ~register_mask(submapper_)) != 0U) {
        reader.fail();
        return;
    }

    latch_ = latch;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
