#include "core/mappers/teletubbies.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr uint16_t kStateTag = 0xED00U;
} // namespace

// Original implementation from NESdev r5823 and BootGod's hardware report.
// Type mode models the examined 420-in-1's forced ROM A1, not a constant menu ID.
Teletubbies::Teletubbies(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {}

Result<void> Teletubbies::validate_geometry(const INesHeader& h,
                                            std::size_t prg_size,
                                            std::size_t chr_size) {
    if (h.is_nes2 && h.submapper != 0U)
        return std::unexpected("Mapper 237 supports submapper 0");
    if (prg_size != 1048576U || chr_size != 0U)
        return std::unexpected("Mapper 237 requires 1 MiB PRG ROM and no CHR ROM");
    if (h.has_battery || (h.is_nes2 && (h.prg_ram_size != 0U || h.prg_nvram_size != 0U ||
                                        h.chr_ram_size != 8192U || h.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 237 requires 8 KiB volatile CHR RAM and no CPU RAM");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 237 supports ordinary H/V CIRAM metadata");
    return {};
}
Byte Teletubbies::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    unsigned inner = data_ & 7U;
    const bool high = addr >= 0xC000U;
    switch (data_ & 0xC0U) {
    case 0U:
        if (high)
            inner = 7U;
        break;
    case 0x40U:
        inner = high ? 7U : inner & 6U;
        break;
    case 0x80U:
        break;
    case 0xC0U:
        inner = (inner & 6U) | static_cast<unsigned>(high);
        break;
    }
    const unsigned bank = ((address_ & 4U) << 3U) | (data_ & 0x18U) | inner;
    unsigned offset = bank * 16384U + (addr & 0x3FFFU);
    if ((address_ & 1U) != 0U)
        offset |= 2U;
    return prg_rom_[offset];
}
void Teletubbies::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    if ((address_ & 2U) != 0U) {
        data_ = static_cast<Byte>((data_ & 0xF8U) | (value & 7U));
        return;
    }
    address_ = static_cast<Byte>(addr & 7U);
    data_ = value;
}
Byte Teletubbies::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}
void Teletubbies::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}
MirrorMode Teletubbies::mirror_mode() const {
    return (data_ & 0x20U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
}
void Teletubbies::power_on() {
    reset();
    chr_ram_.fill(0U);
}
void Teletubbies::reset() {
    address_ = 0U;
    data_ = 0U; // Also clear uncertain t/M/b bits; retain CHR RAM on soft reset.
}
void Teletubbies::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(address_);
    writer.write(data_);
    writer.write_array(chr_ram_);
}
void Teletubbies::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto address = reader.read<Byte>(), data = reader.read<Byte>();
    std::array<Byte, 8192> chr{};
    reader.read_array(chr);
    if (!reader.ok() || tag != kStateTag || address > 7U) {
        reader.fail();
        return;
    }
    address_ = address;
    data_ = data;
    chr_ram_ = chr;
}

} // namespace mapperbus::core
