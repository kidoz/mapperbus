#include "core/mappers/namco175340.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {

// NESdev mapper 210 r23707; submapper selection convention r24451.
Namco175340::Namco175340(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_rom_(std::move(prg)), chr_rom_(std::move(chr)),
      namco175_(header.is_nes2 ? (header.submapper == 1U ||
                                  (header.submapper == 0U &&
                                   (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
                               : header.has_battery),
      prg_ram_size_(namco175_ ? header.prg_ram_size + header.prg_nvram_size : 0U),
      nvram_size_(namco175_ ? header.prg_nvram_size : 0U),
      initial_mirror_(header.mirror_mode == MirrorMode::Horizontal ? 3U : 1U) {
    reset();
}

void Namco175340::reset() {
    // Deterministic defaults, not guaranteed physical power-up values.
    // Retain physical RAM while restoring the disabled CPU access gate.
    prg_banks_ = {0U, 1U, 2U};
    chr_banks_.fill(0U);
    mirror_ = initial_mirror_;
    ram_enabled_ = 0U;
}

bool Namco175340::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && prg_ram_size_ != 0U && ram_enabled_ != 0U);
}

Byte Namco175340::read_prg(Address addr) {
    if (addr < 0x8000U)
        return maps_prg(addr) ? prg_ram_[(addr & 0x1FFFU) % prg_ram_size_] : 0U;
    const auto slot = (addr - 0x8000U) / 0x2000U;
    const std::size_t bank = slot < 3U ? prg_banks_[slot] : prg_rom_.size() / 0x2000U - 1U;
    return prg_rom_[(bank * 0x2000U + (addr & 0x1FFFU)) % prg_rom_.size()];
}

void Namco175340::write_prg(Address address, Byte value) {
    const auto addr = static_cast<unsigned>(address);
    if (addr < 0x8000U) {
        if (addr >= 0x6000U && prg_ram_size_ != 0U && ram_enabled_ != 0U)
            prg_ram_[(addr & 0x1FFFU) % prg_ram_size_] = value;
        return;
    }
    const auto reg = addr & 0xF800U;
    if (reg < 0xC000U) {
        chr_banks_[(reg - 0x8000U) / 0x800U] = value;
    } else if (reg == 0xC000U && namco175_) {
        ram_enabled_ = value & 1U;
    } else if (reg >= 0xE000U && reg < 0xF800U) {
        prg_banks_[(reg - 0xE000U) / 0x800U] = value & 0x3FU;
        if (reg == 0xE000U && !namco175_)
            mirror_ = static_cast<Byte>(static_cast<unsigned>(value) >> 6U);
    }
    // No N163 sound address, IRQ ports, ROM nametables or internal RAM.
}

Byte Namco175340::read_chr(Address addr) {
    const auto offset = static_cast<unsigned>(addr) & 0x1FFFU;
    const auto bank = static_cast<std::size_t>(chr_banks_[offset / 0x400U]);
    return chr_rom_[(bank * 0x400U + (offset & 0x3FFU)) % chr_rom_.size()];
}

void Namco175340::write_chr(Address, Byte) {}

MirrorMode Namco175340::mirror_mode() const {
    constexpr std::array<MirrorMode, 4> kModes = {MirrorMode::SingleLower,
                                                  MirrorMode::Vertical,
                                                  MirrorMode::SingleUpper,
                                                  MirrorMode::Horizontal};
    return kModes[mirror_];
}

std::span<const Byte> Namco175340::battery_ram() const {
    return std::span<const Byte>(prg_ram_).subspan(prg_ram_size_ - nvram_size_, nvram_size_);
}

void Namco175340::set_battery_ram(std::span<const Byte> data) {
    std::copy_n(data.begin(),
                std::min(data.size(), nvram_size_),
                prg_ram_.begin() + static_cast<std::ptrdiff_t>(prg_ram_size_ - nvram_size_));
}

void Namco175340::save_state(StateWriter& writer) const {
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write(mirror_);
    writer.write(ram_enabled_);
    writer.write_array(prg_ram_);
}

void Namco175340::load_state(StateReader& reader) {
    reader.read_array(prg_banks_);
    reader.read_array(chr_banks_);
    reader.read(mirror_);
    reader.read(ram_enabled_);
    reader.read_array(prg_ram_);
    if (!reader.ok() || mirror_ > 3U || ram_enabled_ > 1U ||
        (namco175_ && mirror_ != initial_mirror_) || (!namco175_ && ram_enabled_ != 0U) ||
        std::ranges::any_of(prg_banks_, [](Byte bank) { return bank > 63U; })) {
        reset();
        reader.fail();
    }
}

} // namespace mapperbus::core
