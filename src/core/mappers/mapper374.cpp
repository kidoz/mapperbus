#include "core/mappers/mapper374.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kRomSize = 0x20000UZ;
constexpr std::size_t kGameSize = 0x08000UZ;
constexpr std::size_t kPrgBankSize = 0x4000UZ;
constexpr std::size_t kChrBankSize = 0x1000UZ;
constexpr Byte kGameCount = 4U;
constexpr std::uint16_t kStateTag = 0x3740U;

bool valid_shift(Byte shift, Byte count) {
    if (count > 4U || shift > 0x1FU || (count == 0U && shift != 0x10U)) {
        return false;
    }
    const auto sentinel = static_cast<Byte>(0x10U >> count);
    return (shift & sentinel) != 0U && (shift & (sentinel - 1U)) == 0U;
}

} // namespace

// Original implementation from NESdev mapper 374 oldid 7984. The article
// specifies outer-line increment on M2 interruption; power-on game zero and
// fresh MMC1 registers on each console reset are deterministic policies.
Mapper374::Mapper374(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    power_on();
}

Result<void> Mapper374::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 374U) {
        return std::unexpected("Mapper 374 requires mapper ID 374");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 374 requires NES 2.0 submapper 0");
    }
    if (prg_size != kRomSize || chr_size != kRomSize) {
        return std::unexpected("Mapper 374 requires exactly 128 KiB PRG ROM and 128 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 374 supports ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 374 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

std::size_t Mapper374::prg_address(Address addr) const {
    const auto mode = (registers_.control >> 2U) & 3U;
    const auto selected = registers_.prg_bank & 0x0FU;
    const bool upper = addr >= 0xC000U;
    unsigned bank = 0U;
    switch (mode) {
    case 0U:
    case 1U:
        bank = (selected & 0x0EU) | static_cast<unsigned>(upper);
        break;
    case 2U:
        bank = upper ? selected : 0U;
        break;
    default:
        bank = upper ? 1U : selected;
        break;
    }
    return static_cast<std::size_t>(game_) * kGameSize + (bank & 1U) * kPrgBankSize +
           (addr & 0x3FFFU);
}

std::size_t Mapper374::chr_address(Address addr) const {
    const bool upper = (addr & 0x1000U) != 0U;
    unsigned bank = 0U;
    if ((registers_.control & 0x10U) != 0U) {
        bank = upper ? registers_.chr_bank1 : registers_.chr_bank0;
    } else {
        bank = (registers_.chr_bank0 & 0x1EU) | static_cast<unsigned>(upper);
    }
    return static_cast<std::size_t>(game_) * kGameSize + (bank & 7U) * kChrBankSize +
           (addr & 0x0FFFU);
}

Byte Mapper374::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    return prg_rom_[prg_address(addr) % prg_rom_.size()];
}

void Mapper374::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
    if ((value & 0x80U) != 0U) {
        registers_.shift = 0x10U;
        registers_.write_count = 0U;
        registers_.control |= 0x0CU;
        return;
    }

    registers_.shift = static_cast<Byte>((registers_.shift >> 1U) | ((value & 1U) << 4U));
    ++registers_.write_count;
    if (registers_.write_count == 5U) {
        write_register(addr, registers_.shift);
        registers_.shift = 0x10U;
        registers_.write_count = 0U;
    }
}

void Mapper374::write_prg_cpu(Address addr, Byte value, bool consecutive) {
    if (addr < 0x8000U || !consecutive || (value & 0x80U) != 0U) {
        write_prg(addr, value);
    }
}

void Mapper374::write_register(Address addr, Byte value) {
    value &= 0x1FU;
    if (addr < 0xA000U) {
        registers_.control = value;
    } else if (addr < 0xC000U) {
        registers_.chr_bank0 = value;
    } else if (addr < 0xE000U) {
        registers_.chr_bank1 = value;
    } else {
        registers_.prg_bank = value;
    }
}

bool Mapper374::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper374::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper374::read_chr(Address addr) {
    if (chr_rom_.empty()) {
        return 0U;
    }
    return chr_rom_[chr_address(addr) % chr_rom_.size()];
}

void Mapper374::write_chr(Address, Byte) {}

MirrorMode Mapper374::mirror_mode() const {
    switch (registers_.control & 3U) {
    case 0U:
        return MirrorMode::SingleLower;
    case 1U:
        return MirrorMode::SingleUpper;
    case 2U:
        return MirrorMode::Vertical;
    default:
        return MirrorMode::Horizontal;
    }
}

void Mapper374::power_on() {
    game_ = 0U;
    reset_mmc1();
}

void Mapper374::reset() {
    game_ = static_cast<Byte>((game_ + 1U) % kGameCount);
    reset_mmc1();
}

void Mapper374::reset_mmc1() {
    registers_ = Registers{};
}

void Mapper374::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(game_);
    writer.write(registers_.shift);
    writer.write(registers_.write_count);
    writer.write(registers_.control);
    writer.write(registers_.chr_bank0);
    writer.write(registers_.chr_bank1);
    writer.write(registers_.prg_bank);
}

void Mapper374::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto game = reader.read<Byte>();
    Registers next;
    reader.read(next.shift);
    reader.read(next.write_count);
    reader.read(next.control);
    reader.read(next.chr_bank0);
    reader.read(next.chr_bank1);
    reader.read(next.prg_bank);

    const bool valid = reader.ok() && tag == kStateTag && prg_size == prg_rom_.size() &&
                       chr_size == chr_rom_.size() && game < kGameCount &&
                       valid_shift(next.shift, next.write_count) && next.control <= 0x1FU &&
                       next.chr_bank0 <= 0x1FU && next.chr_bank1 <= 0x1FU && next.prg_bank <= 0x1FU;
    if (!valid) {
        reader.fail();
        return;
    }

    game_ = game;
    registers_ = next;
}

} // namespace mapperbus::core
