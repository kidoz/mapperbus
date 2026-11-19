#include "core/mappers/mapper162_fs304.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

Mapper162Fs304::Mapper162Fs304(const INesHeader& header,
                               std::vector<Byte> prg_rom,
                               std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), mirror_(header.mirror_mode) {
    (void)chr_rom;
}

Result<void> Mapper162Fs304::validate_geometry(const INesHeader& header,
                                               std::size_t prg_size,
                                               std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 162 supports submapper 0");
    if (prg_size < 32768U || prg_size > 2097152U || !std::has_single_bit(prg_size) ||
        chr_size != 0U)
        return std::unexpected("Mapper 162 requires power-of-two 32 KiB-2 MiB PRG and no CHR ROM");
    if (!header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 8192U ||
        header.chr_ram_size != 8192U || header.chr_nvram_size != 0U)
        return std::unexpected("Mapper 162 requires 8 KiB battery WRAM and 8 KiB volatile CHR RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 162 requires fixed H/V mirroring");
    return {};
}

std::size_t Mapper162Fs304::prg_bank() const {
    const unsigned a16 = (mode_ & 0x04U) != 0U ? ((bank_low_ >> 1U) & 1U) : 1U;
    const unsigned a15 = (mode_ & 0x01U) == 0U ? ((bank_alternative_ >> 1U) & 1U)
                                               : ((mode_ & 0x04U) == 0U ? 1U : (bank_low_ & 1U));
    const unsigned bank = (static_cast<unsigned>(bank_high_) << 4U) |
                          (static_cast<unsigned>(bank_low_) & 0x0CU) | (a16 << 1U) | a15;
    return bank & (prg_rom_.size() / 32768U - 1U);
}

Byte Mapper162Fs304::read_prg(Address addr) {
    if (addr >= 0x8000U)
        return prg_rom_[prg_bank() * 32768U + (addr & 0x7FFFU)];
    if (addr >= 0x6000U)
        return wram_[addr & 0x1FFFU];
    return 0U;
}

void Mapper162Fs304::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U)
        wram_[addr & 0x1FFFU] = value;
}

bool Mapper162Fs304::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Address Mapper162Fs304::chr_address(Address addr) const {
    const Address a12 = (bank_low_ & 0x80U) != 0U ? static_cast<Address>(latched_a9_)
                                                  : static_cast<Address>((addr >> 12U) & 1U);
    return static_cast<Address>((addr & 0x0FFFU) | (a12 << 12U));
}

Byte Mapper162Fs304::read_chr(Address addr) {
    return chr_ram_[chr_address(addr)];
}

void Mapper162Fs304::write_chr(Address addr, Byte value) {
    chr_ram_[chr_address(addr)] = value;
}

MirrorMode Mapper162Fs304::mirror_mode() const {
    return mirror_;
}

bool Mapper162Fs304::maps_expansion(Address addr) const {
    return (addr & 0xFF00U) >= 0x5000U && (addr & 0xFF00U) <= 0x5300U;
}

Byte Mapper162Fs304::expansion_read_mask(Address) const {
    return 0U;
}

void Mapper162Fs304::write_expansion(Address addr, Byte value) {
    switch (addr & 0xFF00U) {
    case 0x5000U:
        bank_low_ = value & 0x8FU;
        break;
    case 0x5100U:
        bank_alternative_ = value & 0x02U;
        break;
    case 0x5200U:
        bank_high_ = value & 0x03U;
        break;
    case 0x5300U:
        mode_ = value & 0x05U;
        break;
    default:
        break;
    }
}

bool Mapper162Fs304::observes_ppu_address() const {
    return true;
}

void Mapper162Fs304::observe_ppu_address(Address addr) {
    const bool a13 = (addr & 0x2000U) != 0U;
    if (a13 && !previous_a13_)
        latched_a9_ = (addr & 0x0200U) != 0U;
    previous_a13_ = a13;
}

void Mapper162Fs304::reset() {
    bank_low_ = 0U;
    bank_alternative_ = 0U;
    bank_high_ = 0U;
    mode_ = 0U;
    latched_a9_ = false;
    previous_a13_ = false;
}

void Mapper162Fs304::power_on() {
    reset();
    chr_ram_.fill(0U);
}

std::span<const Byte> Mapper162Fs304::battery_ram() const {
    return wram_;
}

void Mapper162Fs304::set_battery_ram(std::span<const Byte> data) {
    std::copy_n(data.begin(), std::min(data.size(), wram_.size()), wram_.begin());
}

uint16_t Mapper162Fs304::state_tag() const {
    const auto geometry = std::bit_width(prg_rom_.size() / 32768U) - 1U;
    return static_cast<uint16_t>(0xA200U | (geometry << 1U) |
                                 (mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper162Fs304::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(bank_low_);
    writer.write(bank_alternative_);
    writer.write(bank_high_);
    writer.write(mode_);
    writer.write(static_cast<Byte>(latched_a9_));
    writer.write(static_cast<Byte>(previous_a13_));
    writer.write_array(wram_);
    writer.write_array(chr_ram_);
}

void Mapper162Fs304::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto bank_low = reader.read<Byte>();
    const auto bank_alternative = reader.read<Byte>();
    const auto bank_high = reader.read<Byte>();
    const auto mode = reader.read<Byte>();
    const auto latched_a9 = reader.read<Byte>();
    const auto previous_a13 = reader.read<Byte>();
    std::array<Byte, 8192> wram{};
    std::array<Byte, 8192> chr_ram{};
    reader.read_array(wram);
    reader.read_array(chr_ram);
    if (!reader.ok() || tag != state_tag() || (bank_low & 0x70U) != 0U ||
        (bank_alternative & 0xFDU) != 0U || bank_high > 3U || (mode & 0xFAU) != 0U ||
        latched_a9 > 1U || previous_a13 > 1U) {
        reader.fail();
        return;
    }
    bank_low_ = bank_low;
    bank_alternative_ = bank_alternative;
    bank_high_ = bank_high;
    mode_ = mode;
    latched_a9_ = latched_a9 != 0U;
    previous_a13_ = previous_a13 != 0U;
    wram_ = wram;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
