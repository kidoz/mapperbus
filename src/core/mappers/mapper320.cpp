#include "core/mappers/mapper320.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgRomSize = 1024UZ * 1024UZ;
constexpr std::size_t kPrgBankSize = 16384U;
constexpr std::size_t kChrRamSize = 8192U;
constexpr Byte kInnerBankMask = 0x17U;
constexpr std::uint16_t kStateTag = 0xC320U;

} // namespace

Mapper320::Mapper320(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), mirror_(header.mirror_mode) {}

Result<void> Mapper320::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (!header.is_nes2 || header.mapper_number != 320U || header.submapper != 0U) {
        return std::unexpected("Mapper 320 requires NES 2.0 mapper 320 submapper 0");
    }
    if (prg_size != kPrgRomSize || chr_size != 0U) {
        return std::unexpected("Mapper 320 requires the recorded 1 MiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 320 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 320 requires hard-wired H/V mirroring");
    }
    return {};
}

Byte Mapper320::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }

    const bool uorom = (outer_bank_ & 0x10U) == 0U;
    unsigned inner = 0U;
    if (addr >= 0xC000U) {
        inner = uorom ? 15U : 7U;
    } else {
        inner = inner_bank_ & 7U;
        if (uorom) {
            inner |= (inner_bank_ & 0x10U) >> 1U;
        }
    }

    const std::size_t bank = static_cast<std::size_t>(outer_bank_ & 0x0FU) * 8U + inner;
    const std::size_t offset = bank * kPrgBankSize + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper320::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }

    inner_bank_ = value & kInnerBankMask;
    if ((addr & 0xFFE0U) == 0xF0E0U) {
        outer_bank_ = addr & 0x1FU;
    }
}

Byte Mapper320::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper320::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper320::mirror_mode() const {
    return mirror_;
}

void Mapper320::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper320::reset() {
    // Reset circuitry is unspecified; clear both latches deterministically.
    inner_bank_ = 0U;
    outer_bank_ = 0U;
}

std::uint16_t Mapper320::state_tag() const {
    return kStateTag | (mirror_ == MirrorMode::Vertical ? 1U : 0U);
}

void Mapper320::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(inner_bank_);
    writer.write(outer_bank_);
    writer.write_array(chr_ram_);
}

void Mapper320::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto inner_bank = reader.read<Byte>();
    const auto outer_bank = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != state_tag() || (inner_bank & ~kInnerBankMask) != 0U ||
        outer_bank > 0x1FU) {
        reader.fail();
        return;
    }

    inner_bank_ = inner_bank;
    outer_bank_ = outer_bank;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
