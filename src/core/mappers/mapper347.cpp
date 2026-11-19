#include "core/mappers/mapper347.hpp"

#include <optional>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgRomSize = 0x20000UZ;
constexpr std::size_t kPrgRamSize = 0x2000UZ;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::size_t kSwitchableBankSize = 0x1000UZ;
constexpr std::size_t kBank1RomBase = 0x10000UZ;
constexpr std::size_t kFixedRomBase = 0x18000UZ;
constexpr std::uint16_t kStateTag = 0x3470U;

std::optional<std::size_t> prg_ram_offset(Address addr) {
    if (addr >= 0x6000U && addr < 0x6C00U) {
        return addr - 0x6000U;
    }
    if (addr >= 0xB800U && addr < 0xC000U) {
        return 0x0C00UZ + addr - 0xB800U;
    }
    if (addr >= 0xCC00U && addr < 0xD800U) {
        return 0x1400UZ + addr - 0xCC00U;
    }
    return std::nullopt;
}

} // namespace

// Board decoding follows NESdev mapper 347 oldid 7902. Reset defaults and cold
// RAM clearing are deterministic policies because the article does not specify
// them. The original cartridge has no FDS expansion audio or battery storage.
Mapper347::Mapper347(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), initial_mirror_(header.mirror_mode),
      mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper347::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 347U) {
        return std::unexpected("Mapper 347 requires mapper ID 347");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 347 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgRomSize || chr_size != 0U) {
        return std::unexpected("Mapper 347 requires 128 KiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != kPrgRamSize || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 347 requires exactly 8 KiB volatile PRG and CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 347 requires fixed H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper347::read_prg(Address addr) {
    if (const auto ram_offset = prg_ram_offset(addr)) {
        return prg_ram_[*ram_offset];
    }
    if (addr >= 0xC000U && addr < 0xCC00U) {
        return prg_rom_[static_cast<std::size_t>(prg_bank_2_) * kSwitchableBankSize +
                        (addr & 0x0FFFU)];
    }
    if (addr >= 0x8000U) {
        return prg_rom_[kFixedRomBase + addr - 0x8000U];
    }
    if (addr >= 0x7000U && addr < 0x8000U) {
        return prg_rom_[kBank1RomBase +
                        static_cast<std::size_t>(prg_bank_1_) * kSwitchableBankSize +
                        (addr & 0x0FFFU)];
    }
    if (addr >= 0x6C00U) {
        return prg_rom_[static_cast<std::size_t>(prg_bank_2_) * kSwitchableBankSize +
                        (addr & 0x0FFFU)];
    }
    return 0U;
}

void Mapper347::write_prg(Address addr, Byte value) {
    if (const auto ram_offset = prg_ram_offset(addr)) {
        prg_ram_[*ram_offset] = value;
        return;
    }

    switch (addr & 0xF000U) {
    case 0x8000U:
        prg_bank_1_ = value & 0x07U;
        mirror_ = (value & 0x08U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0x9000U:
        prg_bank_2_ = value & 0x0FU;
        break;
    default:
        break;
    }
}

bool Mapper347::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper347::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper347::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper347::mirror_mode() const {
    return mirror_;
}

void Mapper347::power_on() {
    reset();
    prg_ram_.fill(0U);
    chr_ram_.fill(0U);
}

void Mapper347::reset() {
    prg_bank_1_ = 0U;
    prg_bank_2_ = 0U;
    mirror_ = initial_mirror_;
}

std::uint16_t Mapper347::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper347::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(prg_bank_1_);
    writer.write(prg_bank_2_);
    writer.write<Byte>(mirror_ == MirrorMode::Vertical ? 1U : 0U);
    writer.write_array(prg_ram_);
    writer.write_array(chr_ram_);
}

void Mapper347::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto prg_bank_1 = reader.read<Byte>();
    const auto prg_bank_2 = reader.read<Byte>();
    const auto mirror = reader.read<Byte>();
    std::array<Byte, kPrgRamSize> prg_ram{};
    reader.read_array(prg_ram);
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() || prg_bank_1 > 7U ||
        prg_bank_2 > 15U || mirror > 1U) {
        reader.fail();
        return;
    }

    prg_bank_1_ = prg_bank_1;
    prg_bank_2_ = prg_bank_2;
    mirror_ = mirror != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    prg_ram_ = prg_ram;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
