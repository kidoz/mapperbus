#include "core/mappers/mapper409.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x4000000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x4090U;

} // namespace

// Original implementation from NESdev mapper 409 oldid 20235. The article does
// not specify electrical reset; bank zero and RAM-retaining soft reset are
// deterministic compatibility policies. Cold power also clears CHR RAM.
Mapper409::Mapper409(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), initial_mirror_(header.mirror_mode) {
    chr_rom.clear();
    power_on();
}

Result<void> Mapper409::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 409U) {
        return std::unexpected("Mapper 409 requires mapper ID 409");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 409 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        chr_size != 0U) {
        return std::unexpected(
            "Mapper 409 requires 32 KiB-64 MiB power-of-two PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 409 requires exactly 8 KiB volatile CHR RAM and no CPU RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 409 requires hard-wired H/V CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 409 does not provide trainer RAM");
    }
    return {};
}

bool Mapper409::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper409::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper409::read_prg(Address addr) {
    if (!maps_prg(addr) || prg_rom_.empty()) {
        return 0U;
    }
    if (addr >= 0xC000U) {
        return prg_rom_[prg_rom_.size() - kPrgBankSize + (addr & (kPrgBankSize - 1U))];
    }
    const auto address =
        static_cast<std::size_t>(bank_select_) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    return prg_rom_[address & (prg_rom_.size() - 1U)];
}

void Mapper409::write_prg(Address addr, Byte) {
    if (addr >= 0xC000U && addr < 0xD000U) {
        bank_select_ = addr & 0x0FFFU;
    }
}

Byte Mapper409::read_chr(Address addr) {
    return chr_ram_[addr & (kChrRamSize - 1U)];
}

void Mapper409::write_chr(Address addr, Byte value) {
    chr_ram_[addr & (kChrRamSize - 1U)] = value;
}

MirrorMode Mapper409::mirror_mode() const {
    return initial_mirror_;
}

void Mapper409::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper409::reset() {
    bank_select_ = 0U;
}

void Mapper409::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write<std::uint32_t>(kChrRamSize);
    writer.write(static_cast<Byte>(initial_mirror_));
    writer.write(bank_select_);
    writer.write_array(chr_ram_);
}

void Mapper409::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto ram_size = reader.read<std::uint32_t>();
    const auto initial_mirror = reader.read<Byte>();
    const auto bank_select = reader.read<std::uint16_t>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() || chr_size != 0U ||
        ram_size != kChrRamSize || initial_mirror != static_cast<Byte>(initial_mirror_) ||
        bank_select > 0x0FFFU) {
        reader.fail();
        return;
    }

    bank_select_ = bank_select;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
