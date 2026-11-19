#include "core/mappers/mapper521.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x040000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr Address kBankRegisterMask = 0xF030U;
constexpr Address kBankRegisterValue = 0x5020U;
constexpr std::uint16_t kStateTag = 0x5210U;
constexpr Byte kPrgBankMask = 0x0FU;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 521 oldid 22137.
// Reset is unspecified: select bank zero and retain CHR RAM on soft reset;
// cold power clears it deterministically.
Mapper521::Mapper521(const INesHeader& header,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> /*chr_rom*/)
    : prg_rom_(std::move(prg_rom)), initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper521::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 521U) {
        return std::unexpected("Mapper 521 requires mapper ID 521");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 521 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U ||
        chr_size != 0U) {
        return std::unexpected(
            "Mapper 521 requires 32-256 KiB of 16 KiB-banked PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 521 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 521 requires ordinary hard-wired H/V CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 521 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper521::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    if (addr >= 0xC000U) {
        return prg_rom_[prg_rom_.size() - kPrgBankSize + (addr & (kPrgBankSize - 1U))];
    }
    const auto offset =
        static_cast<std::size_t>(prg_bank_) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper521::write_prg(Address /*addr*/, Byte /*value*/) {}

bool Mapper521::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper521::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper521::read_chr(Address addr) {
    return addr < kChrRamSize ? chr_ram_[addr] : 0U;
}

void Mapper521::write_chr(Address addr, Byte value) {
    if (addr < kChrRamSize) {
        chr_ram_[addr] = value;
    }
}

MirrorMode Mapper521::mirror_mode() const {
    return initial_mirror_;
}

void Mapper521::write_expansion(Address addr, Byte value) {
    if ((addr & kBankRegisterMask) == kBankRegisterValue) {
        prg_bank_ = value & kPrgBankMask;
    }
}

void Mapper521::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper521::reset() {
    prg_bank_ = 0U;
}

std::uint16_t Mapper521::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper521::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(prg_bank_);
    writer.write_array(chr_ram_);
}

void Mapper521::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto prg_bank = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() || chr_size != 0U ||
        prg_bank > kPrgBankMask) {
        reader.fail();
        return;
    }

    prg_bank_ = prg_bank;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
