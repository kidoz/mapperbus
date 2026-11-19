#include "core/mappers/mapper481.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x040000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x4810U;
constexpr Byte kBankMask = 0x87U;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 481 oldid 22323.
// Reset clears the latch as a deterministic policy; soft reset retains CHR RAM.
Mapper481::Mapper481(const INesHeader& header,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> /*chr_rom*/)
    : prg_rom_(std::move(prg_rom)), initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper481::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 481U) {
        return std::unexpected("Mapper 481 requires mapper ID 481");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 481 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != 0U) {
        return std::unexpected("Mapper 481 requires exactly 256 KiB PRG ROM and no CHR ROM");
    }
    if (header.prg_ram_size != 0U || header.prg_nvram_size != 0U || header.has_battery) {
        return std::unexpected("Mapper 481 has no CPU work RAM or battery-backed RAM");
    }
    if (header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 481 requires exactly 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 481 requires hard-wired H/V CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 481 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper481::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    const auto inner = addr < 0xC000U ? static_cast<unsigned>(bank_ & 7U) : 7U;
    const auto outer = static_cast<unsigned>((bank_ & 0x80U) >> 4U);
    const auto offset =
        static_cast<std::size_t>(outer | inner) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper481::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U) {
        bank_ = value & kBankMask;
    }
}

bool Mapper481::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper481::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper481::read_chr(Address addr) {
    return addr < kChrRamSize ? chr_ram_[addr] : 0U;
}

void Mapper481::write_chr(Address addr, Byte value) {
    if (addr < kChrRamSize) {
        chr_ram_[addr] = value;
    }
}

MirrorMode Mapper481::mirror_mode() const {
    return initial_mirror_;
}

void Mapper481::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper481::reset() {
    bank_ = 0U;
}

std::uint16_t Mapper481::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper481::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(bank_);
    writer.write_array(chr_ram_);
}

void Mapper481::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto bank = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    const bool valid = reader.ok() && tag == state_tag() && prg_size == prg_rom_.size() &&
                       chr_size == 0U && (bank & ~static_cast<unsigned>(kBankMask)) == 0U;
    if (!valid) {
        reader.fail();
        return;
    }

    bank_ = bank;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
