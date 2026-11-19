#include "core/mappers/mapper122.hpp"

#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

Jy043::Jy043(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), mirror_mode_(header.mirror_mode) {
}

Result<void> Jy043::validate_geometry(const INesHeader& header,
                                      std::size_t prg_size,
                                      std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U) {
        return std::unexpected("Mapper 122 supports submapper 0");
    }
    if (prg_size != 32768U || chr_size != 32768U) {
        return std::unexpected("Mapper 122 requires 32 KiB PRG and 32 KiB CHR ROM");
    }
    if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U))) {
        return std::unexpected("Mapper 122 supports ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 122 requires fixed H/V mirroring");
    }
    return {};
}

Byte Jy043::read_prg(Address addr) {
    return addr >= 0x8000U ? prg_rom_[addr & 0x7FFFU] : 0U;
}

void Jy043::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
    const Byte bank = value & 0x07U;
    if ((addr & 1U) == 0U) {
        chr_bank_low_ = bank;
    } else {
        chr_bank_high_ = bank;
    }
}

Byte Jy043::read_chr(Address addr) {
    const Byte bank = (addr & 0x1000U) == 0U ? chr_bank_low_ : chr_bank_high_;
    return chr_rom_[static_cast<std::size_t>(bank) * 4096U + (addr & 0x0FFFU)];
}

void Jy043::write_chr(Address, Byte) {}

MirrorMode Jy043::mirror_mode() const {
    return mirror_mode_;
}

void Jy043::reset() {
    chr_bank_low_ = 0U;
    chr_bank_high_ = 0U;
}

uint16_t Jy043::state_tag() const {
    return static_cast<uint16_t>(0x7A00U | (mirror_mode_ == MirrorMode::Vertical ? 1U : 0U));
}

void Jy043::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(chr_bank_low_);
    writer.write(chr_bank_high_);
}

void Jy043::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto low = reader.read<Byte>();
    const auto high = reader.read<Byte>();
    if (!reader.ok() || tag != state_tag() || low > 7U || high > 7U) {
        reader.fail();
        return;
    }
    chr_bank_low_ = low;
    chr_bank_high_ = high;
}

} // namespace mapperbus::core
