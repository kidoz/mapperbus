#include "core/mappers/mapper336.hpp"

#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kInnerBankSize = 16384UZ;
constexpr std::size_t kOuterBankSize = 8UZ * kInnerBankSize;
constexpr std::size_t kMaxPrgSize = 8UZ * kOuterBankSize;
constexpr std::size_t kChrRamSize = 8192UZ;
constexpr std::uint16_t kStateTag = 0x3360U;

} // namespace

// Board decoding follows NESdev mapper 336 r24537. Reset circuitry, power-on RAM
// contents and disconnected high ROM pins are unspecified; zeroed power/reset
// state and ROM wrapping are deterministic emulator policies.
Mapper336::Mapper336(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), submapper_(header.submapper) {
    power_on();
}

Result<void> Mapper336::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 336U) {
        return std::unexpected("Mapper 336 requires mapper ID 336");
    }
    if (!header.is_nes2 || header.submapper > 2U) {
        return std::unexpected("Mapper 336 requires NES 2.0 submapper 0, 1, or 2");
    }
    if (prg_size < kOuterBankSize || prg_size > kMaxPrgSize || prg_size % kOuterBankSize != 0U) {
        return std::unexpected("Mapper 336 requires 128 KiB-1 MiB PRG ROM in 128 KiB units");
    }
    if (chr_size != 0U) {
        return std::unexpected("Mapper 336 requires no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 336 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 336 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper336::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }

    const std::size_t outer = static_cast<std::size_t>((latch_ >> 3U) & 7U) * kOuterBankSize;
    const std::size_t inner = addr >= 0xC000U ? 7UZ : static_cast<std::size_t>(latch_ & 7U);
    const std::size_t offset = outer + inner * kInnerBankSize + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper336::write_prg(Address addr, Byte value) {
    if ((addr & 0x8000U) == 0U) {
        return;
    }
    if (submapper_ != 1U) {
        latch_ = value;
        return;
    }

    const Byte rom_value = read_prg(addr);
    latch_ = static_cast<Byte>((value & (rom_value | 0xF8U) & 0xF7U) | (rom_value & 0x08U));
}

Byte Mapper336::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper336::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper336::mirror_mode() const {
    const Byte mirror_bit = submapper_ == 2U ? 0x10U : 0x20U;
    return (latch_ & mirror_bit) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper336::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper336::reset() {
    latch_ = 0U;
}

std::uint16_t Mapper336::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag | submapper_);
}

void Mapper336::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(latch_);
    writer.write_array(chr_ram_);
}

void Mapper336::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto latch = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size()) {
        reader.fail();
        return;
    }

    latch_ = latch;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
