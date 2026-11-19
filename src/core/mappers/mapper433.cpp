#include "core/mappers/mapper433.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x004000UZ;
constexpr std::size_t kMaxPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::uint16_t kStateTag = 0x4330U;
constexpr Byte kResetOuterBank = 0x80U;
constexpr Byte kChrWriteProtect = 0x02U;

} // namespace

// Original implementation from NES 2.0 mapper 433 oldid 8063 and its mapper 233
// equivalence. Cold clearing and A19-toggling reset are deterministic board policies.
Mapper433::Mapper433(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), initial_mirror_(header.mirror_mode) {
    chr_rom.clear();
    power_on();
}

Result<void> Mapper433::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 433U) {
        return std::unexpected("Mapper 433 requires mapper ID 433");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 433 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U) {
        return std::unexpected("Mapper 433 requires 16 KiB-1 MiB PRG ROM in 16 KiB units");
    }
    if (chr_size != 0U) {
        return std::unexpected("Mapper 433 requires no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 433 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 433 requires ordinary H/V CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 433 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper433::read_prg(Address addr) {
    if (addr < 0x8000U || prg_rom_.empty()) {
        return 0U;
    }
    unsigned bank = registers_.control & 0x1FU;
    if ((registers_.control & 0x20U) == 0U) {
        bank = (bank & 0x1EU) | ((addr >> 14U) & 1U);
    }
    bank |= static_cast<unsigned>((registers_.control >> 2U) & 0x20U);
    const auto offset = static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper433::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
    if ((addr & 1U) == 0U) {
        registers_.control = value;
    } else {
        registers_.auxiliary = value & kChrWriteProtect;
    }
}

bool Mapper433::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper433::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper433::read_chr(Address addr) {
    return chr_ram_[addr & (kChrRamSize - 1U)];
}

void Mapper433::write_chr(Address addr, Byte value) {
    if ((registers_.auxiliary & kChrWriteProtect) == 0U) {
        chr_ram_[addr & (kChrRamSize - 1U)] = value;
    }
}

MirrorMode Mapper433::mirror_mode() const {
    return (registers_.control & 0x40U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

void Mapper433::power_on() {
    registers_ = Registers{};
    chr_ram_.fill(0U);
}

void Mapper433::reset() {
    registers_.control = (registers_.control ^ kResetOuterBank) & kResetOuterBank;
    registers_.auxiliary = 0U;
}

std::uint16_t Mapper433::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 0x8000U : 0U));
}

void Mapper433::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(registers_.control);
    writer.write(registers_.auxiliary);
    writer.write_array(chr_ram_);
}

void Mapper433::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.control);
    reader.read(next.auxiliary);
    std::array<Byte, kChrRamSize> next_chr_ram{};
    reader.read_array(next_chr_ram);

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() || chr_size != 0U ||
        (next.auxiliary & ~kChrWriteProtect) != 0U) {
        reader.fail();
        return;
    }

    registers_ = next;
    chr_ram_ = next_chr_ram;
}

} // namespace mapperbus::core
