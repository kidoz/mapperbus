#include "core/mappers/mapper350.hpp"

#include <bit>
#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 16384UZ;
constexpr std::size_t kOuterBankSize = 8UZ * kPrgBankSize;
constexpr std::size_t kMaxPrgSize = 8UZ * kOuterBankSize;
constexpr std::size_t kChrRamSize = 8192UZ;
constexpr std::size_t kFixedPrgOffset = 8192UZ;
constexpr std::uint16_t kStateTag = 0x3500U;

} // namespace

// Board decoding follows NESdev mapper 350 r7909. The article does not specify
// reset values or power-on RAM contents; zeroed registers, vertical reset
// mirroring and zeroed cold CHR RAM are deterministic emulator policies.
Mapper350::Mapper350(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper350::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 350U) {
        return std::unexpected("Mapper 350 requires mapper ID 350");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 350 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kOuterBankSize || prg_size > kMaxPrgSize) {
        return std::unexpected("Mapper 350 requires power-of-two 128-1024 KiB PRG ROM");
    }
    if (chr_size != 0U) {
        return std::unexpected("Mapper 350 requires no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 350 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 350 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

bool Mapper350::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper350::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        return prg_rom_[(kFixedPrgOffset + (addr - 0x6000U)) % prg_rom_.size()];
    }
    if (addr < 0x8000U) {
        return 0U;
    }

    const Byte mode = (outer_bank_ >> 5U) & 3U;
    const Byte outer = mode < 2U ? static_cast<Byte>(outer_bank_ & 0xEFU) : outer_bank_;
    const std::size_t outer_offset = static_cast<std::size_t>((outer >> 1U) & 7U) * kOuterBankSize;
    std::size_t inner = inner_bank_ & 7U;
    if (mode == 1U) {
        inner = (inner & 6U) | ((addr >> 14U) & 1U);
    } else if (mode >= 2U && addr >= 0xC000U) {
        inner = 7U;
    }
    const std::size_t offset = outer_offset + inner * kPrgBankSize + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper350::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
    if (addr < 0xC000U) {
        outer_bank_ = value;
    } else {
        inner_bank_ = value & 7U;
    }
}

Byte Mapper350::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper350::write_chr(Address addr, Byte value) {
    if ((outer_bank_ & 0x40U) != 0U) {
        chr_ram_[addr & 0x1FFFU] = value;
    }
}

MirrorMode Mapper350::mirror_mode() const {
    return (outer_bank_ & 0x80U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper350::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper350::reset() {
    outer_bank_ = 0U;
    inner_bank_ = 0U;
}

void Mapper350::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(outer_bank_);
    writer.write(inner_bank_);
    writer.write_array(chr_ram_);
}

void Mapper350::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto outer_bank = reader.read<Byte>();
    const auto inner_bank = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() || inner_bank > 7U) {
        reader.fail();
        return;
    }

    outer_bank_ = outer_bank;
    inner_bank_ = inner_bank;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
