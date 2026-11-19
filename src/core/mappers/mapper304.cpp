#include "core/mappers/mapper304.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr Address kPrgBankRegister = 0x4027U;
constexpr Address kUnknownReadFirst = 0x4042U;
constexpr Address kUnknownReadLast = 0x4055U;
constexpr std::size_t kPrgBankBytes = 8192U;
constexpr std::uint16_t kStateTag = 0x3040U;

bool is_unknown_read(Address addr) {
    return addr >= kUnknownReadFirst && addr <= kUnknownReadLast;
}

} // namespace

// Original implementation from NESdev mapper 304 r7785 and its linked references.
// Register masks are unknown, so only the published literal write ports are decoded.
Mapper304::Mapper304(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), mirror_mode_(header.mirror_mode),
      has_chr_ram_(chr_rom_.empty()) {
    power_on();
}

Result<void> Mapper304::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (!header.is_nes2 || header.mapper_number != 304U || header.submapper != 0U) {
        return std::unexpected("Mapper 304 requires NES 2.0 mapper 304 submapper 0");
    }
    if (prg_size != 32768U && prg_size != 40960U && prg_size != 49152U) {
        return std::unexpected("Mapper 304 requires 32, 40, or 48 KiB PRG ROM");
    }
    if (chr_size != 0U && chr_size != 8192U) {
        return std::unexpected("Mapper 304 requires unbanked 8 KiB CHR ROM or CHR RAM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != (chr_size == 0U ? 8192U : 0U) || header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 304 requires no persistent memory and only optional 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 304 requires ordinary H/V nametable metadata");
    }
    return {};
}

Byte Mapper304::read_prg(Address addr) {
    if (addr < 0x6000U) {
        return 0U;
    }
    if (addr >= 0x8000U) {
        return prg_rom_[addr - 0x8000U];
    }

    const std::size_t bank = 4U + static_cast<std::size_t>(prg_bank_);
    const std::size_t offset = bank * kPrgBankBytes + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper304::write_prg(Address, Byte) {}

bool Mapper304::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

bool Mapper304::maps_expansion(Address addr) const {
    return is_unknown_read(addr);
}

Byte Mapper304::expansion_read_mask(Address addr) const {
    return is_unknown_read(addr) ? 0xFFU : 0U;
}

Byte Mapper304::read_expansion(Address addr) {
    return is_unknown_read(addr) ? 0xFFU : 0U;
}

void Mapper304::write_expansion(Address addr, Byte value) {
    if (addr == kPrgBankRegister) {
        prg_bank_ = value & 1U;
    }
    // $4068 is consumed by the board. The linked hardware recording indicates
    // no functioning IRQ counter; FCEUX's 5750-cycle threshold is explicitly a
    // guess, so no timer or interrupt pulse is invented here.
}

Byte Mapper304::read_chr(Address addr) {
    const auto offset = static_cast<std::size_t>(addr & 0x1FFFU);
    return has_chr_ram_ ? chr_ram_[offset] : chr_rom_[offset];
}

void Mapper304::write_chr(Address addr, Byte value) {
    if (has_chr_ram_) {
        chr_ram_[addr & 0x1FFFU] = value;
    }
}

MirrorMode Mapper304::mirror_mode() const {
    return mirror_mode_;
}

void Mapper304::power_on() {
    prg_bank_ = 0U;
    if (has_chr_ram_) {
        chr_ram_.fill(0U);
    }
}

void Mapper304::reset() {
    // No reset behavior is documented; retain the bank latch and CHR RAM rather
    // than inventing a reset input. Cold power-on establishes bank four.
}

std::uint16_t Mapper304::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag | (mirror_mode_ == MirrorMode::Vertical ? 1U : 0U) |
                                      (has_chr_ram_ ? 2U : 0U));
}

void Mapper304::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(prg_bank_);
    if (has_chr_ram_) {
        writer.write_array(chr_ram_);
    }
}

void Mapper304::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto prg_bank = reader.read<Byte>();
    std::array<Byte, 8192> chr_ram{};
    if (has_chr_ram_) {
        reader.read_array(chr_ram);
    }

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() || prg_bank > 1U) {
        reader.fail();
        return;
    }

    prg_bank_ = prg_bank;
    if (has_chr_ram_) {
        chr_ram_ = chr_ram;
    }
}

} // namespace mapperbus::core
