#include "core/mappers/multicart230.hpp"

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr uint16_t kStateTag = 0xE600U;
} // namespace

// Original decoding from NESdev mapper 230 r5788. Cold Contra selection,
// reset-cleared bank registers and writable 8 KiB CHR are explicit policies
// where the brief register description does not establish electrical details.
Multicart230::Multicart230(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {}

Result<void> Multicart230::validate_geometry(const INesHeader& h,
                                             std::size_t prg_size,
                                             std::size_t chr_size) {
    if (h.is_nes2 && h.submapper != 0U)
        return std::unexpected("Mapper 230 supports submapper 0");
    if (prg_size != 0xA0000U || chr_size != 0U)
        return std::unexpected("Mapper 230 requires packed 128+512 KiB PRG ROM and no CHR ROM");
    if (h.has_battery || (h.is_nes2 && (h.prg_ram_size != 0U || h.prg_nvram_size != 0U ||
                                        h.chr_ram_size != 8192U || h.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 230 requires 8 KiB volatile CHR RAM and no CPU RAM");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 230 supports ordinary H/V CIRAM");
    return {};
}

Byte Multicart230::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    unsigned page = 0U;
    if (!multicart_) {
        page = addr >= 0xC000U ? 7U : bank_;
    } else {
        page = bank_ & 31U;
        if ((bank_ & 0x20U) == 0U)
            page = (page & 30U) | ((addr >> 14U) & 1U);
        page += 8U;
    }
    return prg_rom_[page * 0x4000U + (addr & 0x3FFFU)];
}

void Multicart230::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U)
        bank_ = value & (multicart_ ? 0x7FU : 7U);
}

Byte Multicart230::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Multicart230::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Multicart230::mirror_mode() const {
    return !multicart_ || (bank_ & 0x40U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
}

void Multicart230::power_on() {
    multicart_ = false;
    bank_ = 0U;
    chr_ram_.fill(0U); // Deterministic cold-RAM policy, distinct from soft reset.
}

void Multicart230::reset() {
    multicart_ = !multicart_;
    bank_ = 0U;
}

void Multicart230::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<Byte>(multicart_));
    writer.write(bank_);
    writer.write_array(chr_ram_);
}

void Multicart230::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto multicart = reader.read<Byte>();
    const auto bank = reader.read<Byte>();
    std::array<Byte, 8192> chr{};
    reader.read_array(chr);
    if (!reader.ok() || tag != kStateTag || multicart > 1U ||
        bank > (multicart != 0U ? 127U : 7U)) {
        // Rejection is not a reset strobe: retain the selected game and RAM.
        reader.fail();
        return;
    }
    multicart_ = multicart != 0U;
    bank_ = bank;
    chr_ram_ = chr;
}

} // namespace mapperbus::core
