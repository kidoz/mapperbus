#include "core/mappers/mapper328.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgRomSize = 16UZ * 1024UZ;
constexpr std::size_t kChrRomSize = 2UZ * 1024UZ;
constexpr std::uint16_t kStateTag = 0x3280U;
constexpr std::uint32_t kInitialWeakBitState = 0x03281994U;
constexpr Byte kWeakBitFixed = 0xF2U;
constexpr Byte kWeakBitMask = 0x0DU;

} // namespace

Mapper328::Mapper328(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), mirror_(header.mirror_mode) {}

Result<void> Mapper328::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (!header.is_nes2 || header.mapper_number != 328U || header.submapper != 0U) {
        return std::unexpected("Mapper 328 requires NES 2.0 mapper 328 submapper 0");
    }
    if (prg_size != kPrgRomSize || chr_size != kChrRomSize) {
        return std::unexpected("Mapper 328 requires exactly 16 KiB PRG ROM and 2 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 328 has no cartridge RAM or battery-backed storage");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 328 requires hard-wired H/V nametable metadata");
    }
    return {};
}

Byte Mapper328::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }

    const bool weak_window =
        (addr >= 0xCE80U && addr <= 0xCEFFU) || (addr >= 0xFE80U && addr <= 0xFEFFU);
    if (weak_window) {
        return static_cast<Byte>(kWeakBitFixed | (next_weak_bits() & kWeakBitMask));
    }
    return prg_rom_[addr & 0x3FFFU];
}

void Mapper328::write_prg(Address, Byte) {}

Byte Mapper328::read_chr(Address addr) {
    return chr_rom_[addr & 0x07FFU];
}

void Mapper328::write_chr(Address, Byte) {}

MirrorMode Mapper328::mirror_mode() const {
    return mirror_;
}

void Mapper328::power_on() {
    reset();
}

void Mapper328::reset() {
    // Analog weak bits have no specified reset sequence; restart deterministically.
    weak_bit_state_ = kInitialWeakBitState;
}

std::uint32_t Mapper328::next_weak_bits() {
    // Xorshift32 simulates changing EPROM bits; it is not a hardware RNG model.
    weak_bit_state_ ^= weak_bit_state_ << 13U;
    weak_bit_state_ ^= weak_bit_state_ >> 17U;
    weak_bit_state_ ^= weak_bit_state_ << 5U;
    return weak_bit_state_;
}

void Mapper328::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(static_cast<Byte>(mirror_ == MirrorMode::Horizontal ? 1U : 0U));
    writer.write(weak_bit_state_);
}

void Mapper328::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto mirror = reader.read<Byte>();
    const auto weak_bit_state = reader.read<std::uint32_t>();

    const auto expected_mirror = static_cast<Byte>(mirror_ == MirrorMode::Horizontal ? 1U : 0U);
    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || mirror != expected_mirror || weak_bit_state == 0U) {
        reader.fail();
        return;
    }

    weak_bit_state_ = weak_bit_state;
}

} // namespace mapperbus::core
