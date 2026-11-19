#include "core/mappers/mapper312.hpp"

#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankBytes = 16384U;
constexpr std::size_t kPrgRomBytes = 8U * kPrgBankBytes;
constexpr std::size_t kChrRamBytes = 8192U;
constexpr Byte kFixedPrgBank = 7U;
constexpr std::uint16_t kStateTag = 0xC312U;

} // namespace

Mapper312::Mapper312(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper312::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 312U) {
        return std::unexpected("Mapper 312 requires mapper ID 312");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 312 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgRomBytes || chr_size != 0U) {
        return std::unexpected("Mapper 312 requires 128 KiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamBytes || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 312 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 312 requires ordinary H/V nametable metadata");
    }
    return {};
}

Byte Mapper312::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    const auto bank = addr >= 0xC000U ? kFixedPrgBank : prg_bank_;
    const auto offset = static_cast<std::size_t>(bank) * kPrgBankBytes + (addr & 0x3FFFU);
    return prg_rom_[offset];
}

void Mapper312::write_prg(Address addr, Byte value) {
    if ((addr & 0xE000U) == 0x6000U) {
        prg_bank_ = value & 0x07U;
    } else if ((addr & 0x8000U) != 0U) {
        mirror_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    }
}

Byte Mapper312::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper312::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper312::mirror_mode() const {
    return mirror_;
}

void Mapper312::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper312::reset() {
    // The pinned article does not establish reset circuitry; these are deterministic defaults.
    prg_bank_ = 0U;
    mirror_ = MirrorMode::Vertical;
}

void Mapper312::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(prg_bank_);
    writer.write<Byte>(mirror_ == MirrorMode::Horizontal ? 1U : 0U);
    writer.write_array(chr_ram_);
}

void Mapper312::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_bank = reader.read<Byte>();
    const auto mirror = reader.read<Byte>();
    std::array<Byte, kChrRamBytes> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != kStateTag || prg_bank > kFixedPrgBank || mirror > 1U) {
        reader.fail();
        return;
    }

    prg_bank_ = prg_bank;
    mirror_ = mirror != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
