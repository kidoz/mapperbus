#include "core/mappers/taito_x1005.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {

// NESdev Taito X1-005 r24449, mapper 207 r24116, pinout r22506.
TaitoX1005::TaitoX1005(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_rom_(std::move(prg)), chr_rom_(std::move(chr)), chr_ciram_(header.mapper_number == 207U),
      battery_(header.has_battery),
      initial_mirror_(header.mirror_mode == MirrorMode::Vertical ? 1U : 0U),
      chr_ram_size_(header.chr_ram_size != 0U ? header.chr_ram_size : 0x2000U) {
    reset();
}

void TaitoX1005::reset() {
    // Stable emulator defaults; neither internal nor CHR RAM is erased.
    prg_banks_ = {0U, 1U, 2U};
    chr_banks_.fill(0U);
    mirror_ = initial_mirror_;
    ram_enabled_ = 0U;
}

bool TaitoX1005::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x7F00U && ram_enabled_ != 0U);
}

Byte TaitoX1005::read_prg(Address addr) {
    if (addr < 0x8000U)
        return addr >= 0x7F00U && ram_enabled_ != 0U ? internal_ram_[addr & 0x7FU] : 0U;
    const auto slot = (addr - 0x8000U) / 0x2000U;
    const std::size_t bank = slot < 3U ? prg_banks_[slot] : prg_rom_.size() / 0x2000U - 1U;
    return prg_rom_[(bank * 0x2000U + (addr & 0x1FFFU)) % prg_rom_.size()];
}

void TaitoX1005::write_prg(Address address, Byte value) {
    const auto addr = static_cast<unsigned>(address);
    if (addr >= 0x7F00U && addr < 0x8000U) {
        if (ram_enabled_ != 0U)
            internal_ram_[addr & 0x7FU] = value;
        return;
    }
    // A7 is not connected: only $7E70-$7E7F and $7EF0-$7EFF decode.
    if ((addr & 0xFF70U) != 0x7E70U)
        return;
    const auto reg = addr & 15U;
    if (reg < 6U) {
        chr_banks_[reg] = reg < 2U ? value & 0xFEU : value;
    } else if (reg == 6U) {
        if (!chr_ciram_)
            mirror_ = value & 1U;
    } else if (reg == 8U || reg == 9U) {
        ram_enabled_ = value == 0xA3U ? 1U : 0U;
    } else if (reg >= 10U) {
        prg_banks_[(reg - 10U) / 2U] = value & 0x3FU;
    }
    // $7EF7/$7E77 has no known effect; it is not a mirroring alias.
}

std::size_t TaitoX1005::chr_offset(Address address) const {
    const auto addr = static_cast<unsigned>(address) & 0x1FFFU;
    if (addr < 0x1000U)
        return static_cast<std::size_t>(chr_banks_[addr / 0x800U]) * 0x400U + (addr & 0x7FFU);
    return static_cast<std::size_t>(chr_banks_[2U + (addr - 0x1000U) / 0x400U]) * 0x400U +
           (addr & 0x3FFU);
}

Byte TaitoX1005::read_chr(Address addr) {
    const auto offset = chr_offset(addr);
    return chr_rom_.empty() ? chr_ram_[offset % chr_ram_size_] : chr_rom_[offset % chr_rom_.size()];
}

void TaitoX1005::write_chr(Address addr, Byte value) {
    if (chr_rom_.empty())
        chr_ram_[chr_offset(addr) % chr_ram_size_] = value;
}

MirrorMode TaitoX1005::mirror_mode() const {
    if (!chr_ciram_)
        return mirror_ != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    const bool first = (chr_banks_[0U] & 0x80U) != 0U;
    const bool second = (chr_banks_[1U] & 0x80U) != 0U;
    if (first == second)
        return first ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    // The reversed pair is also horizontal in shape; nametable_address()
    // supplies the exact page order, which MirrorMode cannot express.
    return MirrorMode::Horizontal;
}

std::optional<Address> TaitoX1005::nametable_address(Address addr) const {
    if (!chr_ciram_)
        return std::nullopt;
    const auto pair = (static_cast<unsigned>(addr) >> 11U) & 1U;
    const auto page = (chr_banks_[pair] & 0x80U) != 0U ? 0x400U : 0U;
    return static_cast<Address>(page | (addr & 0x3FFU));
}

std::span<const Byte> TaitoX1005::battery_ram() const {
    return battery_ ? std::span<const Byte>(internal_ram_) : std::span<const Byte>{};
}

void TaitoX1005::set_battery_ram(std::span<const Byte> data) {
    if (battery_)
        std::copy_n(
            data.begin(), std::min(data.size(), internal_ram_.size()), internal_ram_.begin());
}

void TaitoX1005::save_state(StateWriter& writer) const {
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write(mirror_);
    writer.write(ram_enabled_);
    writer.write_array(internal_ram_);
    writer.write_array(chr_ram_);
}

void TaitoX1005::load_state(StateReader& reader) {
    reader.read_array(prg_banks_);
    reader.read_array(chr_banks_);
    reader.read(mirror_);
    reader.read(ram_enabled_);
    reader.read_array(internal_ram_);
    reader.read_array(chr_ram_);
    if (!reader.ok() || mirror_ > 1U || ram_enabled_ > 1U || (chr_banks_[0U] & 1U) != 0U ||
        (chr_banks_[1U] & 1U) != 0U ||
        std::ranges::any_of(prg_banks_, [](Byte bank) { return bank > 0x3FU; })) {
        reset();
        reader.fail();
    }
}

} // namespace mapperbus::core
