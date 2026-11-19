#include "core/mappers/mapper497.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x008000UZ;
constexpr std::size_t kChrRamSize = 0x008000UZ;
constexpr std::size_t kChrBankSize = 0x001000UZ;
constexpr std::uint16_t kStateTag = 0x4970U;
constexpr Byte kModeMask = 0x03U;
constexpr Byte kNametableMask = 0x03U;
constexpr Byte kStateFlagsMask = 0x1FU;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 497 oldid 22873.
// Reset is unspecified; zeroed latches and retained RAM are the soft-reset policy.
Mapper497::Mapper497(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), initial_mirror_(header.mirror_mode) {
    chr_rom.clear();
    power_on();
}

Result<void> Mapper497::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 497U) {
        return std::unexpected("Mapper 497 requires mapper ID 497");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 497 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != 0U) {
        return std::unexpected("Mapper 497 requires exactly 32 KiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 497 requires only 32 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 497 requires ordinary H/V CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 497 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper497::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    const auto offset = static_cast<std::size_t>(addr - 0x8000U);
    return offset < prg_rom_.size() ? prg_rom_[offset] : 0U;
}

void Mapper497::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U) {
        registers_.mode = value & kModeMask;
    }
}

bool Mapper497::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper497::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

std::size_t Mapper497::chr_offset(Address addr) const {
    if (addr < kChrBankSize) {
        return addr;
    }
    Byte bank = 1U;
    if (registers_.mode >= 2U &&
        (registers_.mode != 3U || registers_.nametable_bits != kNametableMask)) {
        bank = registers_.nametable_bits + 2U;
    }
    return static_cast<std::size_t>(bank) * kChrBankSize + (addr & (kChrBankSize - 1U));
}

Byte Mapper497::read_chr(Address addr) {
    return addr < 0x2000U ? chr_ram_[chr_offset(addr)] : 0U;
}

void Mapper497::write_chr(Address addr, Byte value) {
    if (addr < 0x2000U) {
        chr_ram_[chr_offset(addr)] = value;
    }
}

MirrorMode Mapper497::mirror_mode() const {
    return initial_mirror_;
}

bool Mapper497::observes_ppu_address() const {
    return true;
}

void Mapper497::observe_ppu_address(Address addr) {
    const bool a13_high = (addr & 0x2000U) != 0U;
    const bool nametable_fetch = (addr & 0x3000U) == 0x2000U && (addr & 0x03C0U) != 0x03C0U;
    if (a13_high && !registers_.a13_high && nametable_fetch) {
        registers_.nametable_bits = (addr >> 8U) & kNametableMask;
    }
    registers_.a13_high = a13_high;
}

void Mapper497::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper497::reset() {
    registers_ = Registers{};
}

std::uint16_t Mapper497::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper497::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    const Byte flags = static_cast<Byte>(registers_.mode | (registers_.nametable_bits << 2U) |
                                         (registers_.a13_high ? 0x10U : 0U));
    writer.write(flags);
    writer.write_array(chr_ram_);
}

void Mapper497::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto flags = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() || chr_size != 0U ||
        (flags & ~static_cast<unsigned>(kStateFlagsMask)) != 0U) {
        reader.fail();
        return;
    }

    Registers next;
    next.mode = flags & kModeMask;
    next.nametable_bits = (flags >> 2U) & kNametableMask;
    next.a13_high = (flags & 0x10U) != 0U;
    registers_ = next;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
