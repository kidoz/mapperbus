#include "core/mappers/mapper103.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr uint16_t kStateTag = 0x6700U;
} // namespace

Mapper103::Mapper103(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), initial_mirror_(header.mirror_mode), mirror_(initial_mirror_) {}

Result<void> Mapper103::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 103 supports submapper 0");
    if (prg_size != 131072U || chr_size != 0U)
        return std::unexpected("Mapper 103 requires 128 KiB PRG ROM and no CHR ROM");
    if (header.has_battery || header.prg_nvram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 16384U || header.chr_ram_size != 8192U)))
        return std::unexpected("Mapper 103 requires 16 KiB volatile PRG RAM and 8 KiB CHR RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 103 supports ordinary H/V CIRAM");
    return {};
}

bool Mapper103::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper103::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (ram_disabled_ == 0U)
            return prg_ram_[8192U + addr - 0x6000U];
        return prg_rom_[prg_bank_ * 8192U + addr - 0x6000U];
    }
    if (addr >= 0x8000U && addr < 0xB800U)
        return prg_rom_[0x18000U + addr - 0x8000U];
    if (addr >= 0xB800U && addr < 0xD800U) {
        if (ram_disabled_ == 0U)
            return prg_ram_[addr - 0xB800U];
        return prg_rom_[0x1B800U + addr - 0xB800U];
    }
    if (addr >= 0xD800U)
        return prg_rom_[0x1D800U + addr - 0xD800U];
    return 0U;
}

void Mapper103::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        prg_ram_[8192U + addr - 0x6000U] = value;
        return;
    }
    if (addr >= 0xB800U && addr < 0xD800U) {
        prg_ram_[addr - 0xB800U] = value;
        return;
    }
    switch (addr & 0xF000U) {
    case 0x8000U:
        prg_bank_ = value & 15U;
        break;
    case 0xE000U:
        mirror_ = (value & 0x10U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xF000U:
        ram_disabled_ = (value & 0x10U) != 0U ? 1U : 0U;
        break;
    default:
        break;
    }
}

Byte Mapper103::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper103::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper103::mirror_mode() const {
    return mirror_;
}

void Mapper103::power_on() {
    prg_bank_ = 0U;
    ram_disabled_ = 0U;
    mirror_ = initial_mirror_;
    prg_ram_.fill(0U);
    chr_ram_.fill(0U);
}

void Mapper103::reset() {
    // The board has no register-clearing reset input; RAM and latches are retained.
}

void Mapper103::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(initial_mirror_);
    writer.write(prg_bank_);
    writer.write(ram_disabled_);
    writer.write(mirror_);
    writer.write_array(prg_ram_);
    writer.write_array(chr_ram_);
}

void Mapper103::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto initial_mirror = reader.read<MirrorMode>();
    const auto prg_bank = reader.read<Byte>();
    const auto ram_disabled = reader.read<Byte>();
    const auto mirror = reader.read<MirrorMode>();
    std::array<Byte, 16384> prg_ram{};
    std::array<Byte, 8192> chr_ram{};
    reader.read_array(prg_ram);
    reader.read_array(chr_ram);
    const auto valid_mirror = [](MirrorMode value) {
        return value == MirrorMode::Horizontal || value == MirrorMode::Vertical;
    };
    if (!reader.ok() || tag != kStateTag || initial_mirror != initial_mirror_ || prg_bank > 15U ||
        ram_disabled > 1U || !valid_mirror(mirror)) {
        reader.fail();
        return;
    }
    prg_bank_ = prg_bank;
    ram_disabled_ = ram_disabled;
    mirror_ = mirror;
    prg_ram_ = prg_ram;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
