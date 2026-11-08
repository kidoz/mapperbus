#include "core/mappers/namco108.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {

// https://www.nesdev.org/wiki/INES_Mapper_206 and its 76/88/95/154 variants.
Namco108::Namco108(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_rom_(std::move(prg)), chr_rom_(std::move(chr)), mapper_(header.mapper_number),
      initial_mirror_(header.mirror_mode),
      fixed_prg_(mapper_ == 206U && header.is_nes2 && header.submapper == 1U),
      has_ram_(mapper_ == 206U &&
               (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U)),
      mirror_(initial_mirror_) {
    reset();
}

void Namco108::reset() {
    select_ = 0U;
    banks_.fill(0U);
    banks_[7U] = 1U;
    mirror_ = mapper_ == 154U ? MirrorMode::SingleLower : initial_mirror_;
}

bool Namco108::maps_prg(Address addr) const {
    return addr >= 0x8000U || (has_ram_ && addr >= 0x6000U);
}

Byte Namco108::read_prg(Address addr) {
    if (addr < 0x8000U)
        return has_ram_ && addr >= 0x6000U ? prg_ram_[addr & 0x1FFFU] : 0U;
    if (prg_rom_.empty())
        return 0U;
    if (fixed_prg_)
        return prg_rom_[(addr & 0x7FFFU) % prg_rom_.size()];
    const auto slot = (addr - 0x8000U) >> 13U;
    const std::size_t bank =
        slot < 2U ? banks_[6U + slot] & 15U : prg_rom_.size() / 0x2000U - 4U + slot;
    return prg_rom_[(bank * 0x2000U + (addr & 0x1FFFU)) % prg_rom_.size()];
}

void Namco108::write_prg(Address addr, Byte value) {
    if (has_ram_ && addr >= 0x6000U && addr < 0x8000U)
        prg_ram_[addr & 0x1FFFU] = value;
    if (addr < 0x8000U)
        return;
    if (mapper_ == 154U)
        mirror_ = (value & 0x40U) != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    if (addr >= 0xA000U)
        return;
    if ((addr & 1U) == 0U)
        select_ = value & 7U;
    else
        banks_[select_] = value & 0x3FU;
}

std::size_t Namco108::chr_offset(Address address) const {
    const auto addr = static_cast<unsigned>(address);
    if (mapper_ == 76U)
        return banks_[2U + (addr >> 11U)] * 0x800U + (addr & 0x07FFU);
    std::size_t bank = 0;
    if (addr < 0x1000U)
        bank = (banks_[addr >> 11U] & 0x3EU) | ((addr >> 10U) & 1U);
    else
        bank = banks_[2U + ((addr - 0x1000U) >> 10U)] & 0x3FU;
    if ((mapper_ == 88U || mapper_ == 154U) && addr >= 0x1000U)
        bank |= 0x40U;
    return bank * 0x400U + (addr & 0x03FFU);
}

Byte Namco108::read_chr(Address addr) {
    const auto offset = chr_offset(addr & 0x1FFFU);
    return chr_rom_.empty() ? chr_ram_[offset % chr_ram_.size()]
                            : chr_rom_[offset % chr_rom_.size()];
}

void Namco108::write_chr(Address addr, Byte value) {
    if (chr_rom_.empty())
        chr_ram_[chr_offset(addr & 0x1FFFU) % chr_ram_.size()] = value;
}

MirrorMode Namco108::mirror_mode() const {
    return mirror_;
}

std::optional<Address> Namco108::nametable_address(Address addr) const {
    if (mapper_ != 95U)
        return std::nullopt;
    const auto index = (static_cast<unsigned>(addr) >> 11U) & 1U;
    const auto bank = (static_cast<unsigned>(banks_[index]) >> 5U) & 1U;
    return static_cast<Address>(bank * 0x400U + (addr & 0x03FFU));
}

void Namco108::save_state(StateWriter& writer) const {
    writer.write(select_);
    writer.write_array(banks_);
    writer.write(mirror_);
    writer.write_array(prg_ram_);
    writer.write_array(chr_ram_);
}

void Namco108::load_state(StateReader& reader) {
    reader.read(select_);
    reader.read_array(banks_);
    reader.read(mirror_);
    reader.read_array(prg_ram_);
    reader.read_array(chr_ram_);
    if (select_ > 7U || mirror_ > MirrorMode::SingleUpper ||
        std::ranges::any_of(banks_, [](Byte bank) { return bank > 0x3FU; })) {
        reset();
        reader.fail();
    }
}

std::span<const Byte> Namco108::battery_ram() const {
    return has_ram_ ? std::span<const Byte>(prg_ram_) : std::span<const Byte>{};
}

void Namco108::set_battery_ram(std::span<const Byte> data) {
    if (has_ram_)
        std::copy_n(data.begin(), std::min(data.size(), prg_ram_.size()), prg_ram_.begin());
}

} // namespace mapperbus::core
