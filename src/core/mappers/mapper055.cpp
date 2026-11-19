#include "core/mappers/mapper055.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr uint16_t kStateTag = 0x3700U;
} // namespace

// Original implementation from NESdev mapper 055 r4324.
Mario1Malee2::Mario1Malee2(const INesHeader& header,
                           std::vector<Byte> prg_rom,
                           std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), mirror_mode_(header.mirror_mode) {
}

Result<void> Mario1Malee2::validate_geometry(const INesHeader& header,
                                             std::size_t prg_size,
                                             std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 055 supports submapper 0");
    if (prg_size != 49152U || chr_size != 8192U)
        return std::unexpected("Mapper 055 requires 48 KiB PRG ROM and 8 KiB CHR ROM");
    if (header.has_battery ||
        (header.is_nes2 && (header.prg_ram_size != 2048U || header.prg_nvram_size != 0U ||
                            header.chr_ram_size != 0U || header.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 055 requires 2 KiB volatile PRG RAM and no CHR RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 055 supports ordinary H/V CIRAM");
    return {};
}

Byte Mario1Malee2::read_prg(Address addr) {
    if (addr >= 0x8000U)
        return prg_rom_[addr - 0x8000U];
    if (addr >= 0x7000U)
        return prg_ram_[addr & 0x07FFU];
    if (addr >= 0x6000U)
        return prg_rom_[0x8000U + (addr & 0x07FFU)];
    return 0U;
}

void Mario1Malee2::write_prg(Address addr, Byte value) {
    if (addr >= 0x7000U && addr < 0x8000U)
        prg_ram_[addr & 0x07FFU] = value;
}

std::span<Byte> Mario1Malee2::trainer_ram() {
    return prg_ram_;
}

bool Mario1Malee2::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mario1Malee2::read_chr(Address addr) {
    return chr_rom_[addr & 0x1FFFU];
}

void Mario1Malee2::write_chr(Address, Byte) {}

MirrorMode Mario1Malee2::mirror_mode() const {
    return mirror_mode_;
}

void Mario1Malee2::power_on() {
    prg_ram_.fill(0U);
}

void Mario1Malee2::reset() {}

void Mario1Malee2::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write_array(prg_ram_);
}

void Mario1Malee2::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    std::array<Byte, 2048> prg_ram{};
    reader.read_array(prg_ram);
    if (!reader.ok() || tag != kStateTag) {
        reader.fail();
        return;
    }
    prg_ram_ = prg_ram;
}

} // namespace mapperbus::core
