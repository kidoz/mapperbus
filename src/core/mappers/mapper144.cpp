#include "core/mappers/mapper144.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

Mapper144::Mapper144(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), mirror_mode_(header.mirror_mode) {
}

Result<void> Mapper144::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 144 supports submapper 0");
    if (prg_size != 65536U || chr_size != 65536U)
        return std::unexpected("Mapper 144 requires 64 KiB PRG and 64 KiB CHR ROM");
    if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
        return std::unexpected("Mapper 144 supports ROM-only board memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 144 supports fixed H/V mirroring");
    return {};
}

Byte Mapper144::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    const std::size_t offset = static_cast<std::size_t>(latch_ & 0x03U) * 0x8000U +
                               static_cast<std::size_t>(addr & 0x7FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper144::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    const Byte rom_value = read_prg(addr);
    latch_ = static_cast<Byte>(rom_value & (value | 0x01U));
}

Byte Mapper144::read_chr(Address addr) {
    const std::size_t offset =
        static_cast<std::size_t>(latch_ >> 4U) * 0x2000U + static_cast<std::size_t>(addr & 0x1FFFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper144::write_chr(Address, Byte) {}

MirrorMode Mapper144::mirror_mode() const {
    return mirror_mode_;
}

void Mapper144::power_on() {
    latch_ = 0U;
}

void Mapper144::reset() {
    // The 74LS377 has no reset input; preserve the latch on a console reset.
}

std::uint16_t Mapper144::state_tag() const {
    return static_cast<std::uint16_t>(0x9000U | (mirror_mode_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper144::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(latch_);
}

void Mapper144::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto latch = reader.read<Byte>();
    if (!reader.ok() || tag != state_tag()) {
        reader.fail();
        return;
    }
    latch_ = latch;
}

} // namespace mapperbus::core
