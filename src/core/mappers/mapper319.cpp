#include "core/mappers/mapper319.hpp"

#include <bit>
#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankBytes = 16384U;
constexpr std::size_t kChrBankBytes = 8192U;
constexpr std::size_t kMaxPrgBytes = 8U * kPrgBankBytes;
constexpr std::size_t kMaxChrBytes = 16U * kChrBankBytes;
constexpr std::uint16_t kStateTag = 0x3190U;

} // namespace

Mapper319::Mapper319(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> Mapper319::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 319U) {
        return std::unexpected("Mapper319 requires mapper ID 319");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 319 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kPrgBankBytes || prg_size > kMaxPrgBytes ||
        !std::has_single_bit(chr_size) || chr_size < kChrBankBytes || chr_size > kMaxChrBytes) {
        return std::unexpected(
            "Mapper 319 requires power-of-two 16-128 KiB PRG and 8-128 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 319 has no cartridge RAM, NVRAM, or battery");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 319 requires ordinary H/V nametable metadata");
    }
    return {};
}

Byte Mapper319::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }

    const unsigned high = (prg_control_ >> 4U) & 1U;
    const unsigned middle = (prg_control_ >> 3U) & 1U;
    const unsigned low =
        (prg_control_ & 0x40U) != 0U ? ((addr >> 14U) & 1U) : ((prg_control_ >> 5U) & 1U);
    const auto bank = (high << 2U) | (middle << 1U) | low;
    const auto offset = bank * kPrgBankBytes + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper319::write_prg(Address addr, Byte value) {
    switch (addr & 0x6004U) {
    case 0x6000U:
        chr_bank_ = value & 0xF0U;
        break;
    case 0x6004U:
        prg_control_ = value & 0xF8U;
        break;
    default:
        break;
    }
}

Byte Mapper319::read_chr(Address addr) {
    const auto bank = static_cast<std::size_t>(chr_bank_ >> 4U);
    const auto offset = bank * kChrBankBytes + (addr & 0x1FFFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper319::write_chr(Address, Byte) {}

MirrorMode Mapper319::mirror_mode() const {
    return (prg_control_ & 0x80U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
}

bool Mapper319::maps_expansion(Address addr) const {
    return (addr & 0xFFF0U) == 0x5FF0U;
}

Byte Mapper319::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0x10U : 0U;
}

Byte Mapper319::read_expansion(Address addr) {
    return maps_expansion(addr) ? static_cast<Byte>(solder_pad_ << 4U) : 0U;
}

Result<void> Mapper319::configure_board_jumpers(Byte value) {
    if (value > 1U) {
        return std::unexpected("Mapper 319 solder pad requires a value of 0 or 1");
    }
    solder_pad_ = value;
    return {};
}

void Mapper319::reset() {
    // The pinned description has no reset input; zero latches are a deterministic policy.
    prg_control_ = 0U;
    chr_bank_ = 0U;
}

std::uint16_t Mapper319::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag | solder_pad_);
}

void Mapper319::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(prg_control_);
    writer.write(chr_bank_);
}

void Mapper319::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto prg_control = reader.read<Byte>();
    const auto chr_bank = reader.read<Byte>();

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || (prg_control & 0x07U) != 0U || (chr_bank & 0x0FU) != 0U) {
        reader.fail();
        return;
    }

    prg_control_ = prg_control;
    chr_bank_ = chr_bank;
}

} // namespace mapperbus::core
