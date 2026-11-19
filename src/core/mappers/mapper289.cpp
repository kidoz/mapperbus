#include "core/mappers/mapper289.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t kPrgBankBytes = 16384U;
constexpr std::size_t kMaxPrgBytes = 128U * kPrgBankBytes;
constexpr std::size_t kChrRamBytes = 8192U;
} // namespace

// Original implementation from the immutable NESdev mapper 289 article, r20161.
Mapper289::Mapper289(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {}

Result<void> Mapper289::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 289U) {
        return std::unexpected("Mapper289 requires mapper ID 289");
    }
    if (header.is_nes2 && header.submapper != 0U) {
        return std::unexpected("Mapper 289 supports submapper 0");
    }
    if (prg_size < kPrgBankBytes || prg_size > kMaxPrgBytes || prg_size % kPrgBankBytes != 0U ||
        chr_size != 0U) {
        return std::unexpected(
            "Mapper 289 requires 16 KiB-2 MiB PRG in 16 KiB units and no CHR ROM");
    }
    if (header.has_battery || header.prg_nvram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.chr_ram_size != kChrRamBytes))) {
        return std::unexpected(
            "Mapper 289 requires 8 KiB volatile CHR RAM and no persistent or CPU RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 289 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

bool Mapper289::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper289::prg_read_mask(Address addr) const {
    return addr >= 0x6000U && addr < 0x8000U ? 0x03U : 0xFFU;
}

Byte Mapper289::selected_prg_bank(Address addr) const {
    switch (mode_ & 3U) {
    case 0U:
        return outer_bank_;
    case 1U:
        return static_cast<Byte>((outer_bank_ & 0x7EU) | ((addr >> 14U) & 1U));
    case 2U:
        return static_cast<Byte>((outer_bank_ & 0x78U) | (addr < 0xC000U ? data_latch_ : 7U));
    default:
        return static_cast<Byte>((outer_bank_ & 0x78U) | 7U);
    }
}

Byte Mapper289::read_prg(Address addr) {
    if (addr >= 0x8000U) {
        const std::size_t offset =
            static_cast<std::size_t>(selected_prg_bank(addr)) * kPrgBankBytes + (addr & 0x3FFFU);
        return prg_rom_[offset % prg_rom_.size()];
    }
    if ((addr & 0xE000U) == 0x6000U) {
        return solder_pads_;
    }
    return 0U;
}

void Mapper289::write_prg(Address addr, Byte value) {
    if ((addr & 0xE001U) == 0x6000U) {
        mode_ = value & 0x0FU;
    } else if ((addr & 0xE001U) == 0x6001U) {
        outer_bank_ = value & 0x7FU;
    } else if (addr >= 0x8000U) {
        data_latch_ = value & 7U;
    }
}

Byte Mapper289::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper289::write_chr(Address addr, Byte value) {
    if ((mode_ & 4U) == 0U) {
        chr_ram_[addr & 0x1FFFU] = value;
    }
}

MirrorMode Mapper289::mirror_mode() const {
    return (mode_ & 8U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

Result<void> Mapper289::configure_board_jumpers(Byte value) {
    if (value > 3U) {
        return std::unexpected("Mapper 289 solder pads require a value 0..3");
    }
    solder_pads_ = value;
    return {};
}

void Mapper289::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper289::reset() {
    // Deterministic latch policy; r20161 does not specify electrical reset behavior.
    mode_ = 0U;
    outer_bank_ = 0U;
    data_latch_ = 0U;
}

uint16_t Mapper289::state_tag() const {
    return static_cast<uint16_t>(0xF800U | solder_pads_);
}

void Mapper289::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<uint32_t>(prg_rom_.size()));
    writer.write(mode_);
    writer.write(outer_bank_);
    writer.write(data_latch_);
    writer.write_array(chr_ram_);
}

void Mapper289::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_size = reader.read<uint32_t>();
    const auto mode = reader.read<Byte>();
    const auto outer_bank = reader.read<Byte>();
    const auto data_latch = reader.read<Byte>();
    std::array<Byte, kChrRamBytes> chr{};
    reader.read_array(chr);
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() || mode > 0x0FU ||
        outer_bank > 0x7FU || data_latch > 7U) {
        reader.fail();
        return;
    }
    mode_ = mode;
    outer_bank_ = outer_bank;
    data_latch_ = data_latch;
    chr_ram_ = chr;
}

} // namespace mapperbus::core
