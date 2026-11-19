#include "core/mappers/nanjing_fc001.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr uint16_t kStateTag = 0xA300U;

Byte register_value(Byte value, Byte mode) {
    if ((mode & 1U) == 0U)
        return value;
    return static_cast<Byte>((value & 0xFCU) | ((value & 1U) << 1U) | ((value & 2U) >> 1U));
}
} // namespace

// Original implementation from NESdev mapper 163 r24182. Submapper 1 also
// requires cartridge access to the $4016/$4017 ADPCM handshake pins.
NanjingFc001::NanjingFc001(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), mirror_(header.mirror_mode) {
    power_on();
}

Result<void> NanjingFc001::validate_geometry(const INesHeader& header,
                                             std::size_t prg_size,
                                             std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U) {
        if (header.submapper == 1U)
            return std::unexpected(
                "Mapper 163 submapper 1 requires unsupported $4016/$4017 ADPCM GPIO routing");
        return std::unexpected("Mapper 163 supports only NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < 32768U || prg_size > 2097152U ||
        chr_size != 0U)
        return std::unexpected("Mapper 163 requires power-of-two 32 KiB-2 MiB PRG and no CHR ROM");
    if (!header.has_battery ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 8192U ||
                            header.chr_ram_size != 8192U || header.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 163 requires 8 KiB PRG NVRAM and 8 KiB volatile CHR RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 163 requires fixed H/V mirroring");
    return {};
}

std::size_t NanjingFc001::prg_bank() const {
    const auto low = static_cast<std::size_t>(bank_low_ & 0x0FU) | ((mode_ & 4U) == 0U ? 3U : 0U);
    // On 1 MiB boards both high outputs drive ROM A19, so their OR is independent
    // of the mode-controlled D0/D1 exchange.
    const auto high = prg_rom_.size() == 1048576U ? (bank_high_ != 0U ? 0x10U : 0U)
                                                  : static_cast<std::size_t>(bank_high_) << 4U;
    return high | low;
}

Byte NanjingFc001::read_prg(Address addr) {
    if (addr >= 0x8000U)
        return prg_rom_[(prg_bank() * 32768U + (addr & 0x7FFFU)) % prg_rom_.size()];
    if (addr >= 0x6000U)
        return wram_[addr & 0x1FFFU];
    return 0U;
}

void NanjingFc001::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U)
        wram_[addr & 0x1FFFU] = value;
}

bool NanjingFc001::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Address NanjingFc001::chr_address(Address addr) const {
    addr &= 0x1FFFU;
    if ((bank_low_ & 0x80U) == 0U)
        return addr;
    return static_cast<Address>((addr & 0x0FFFU) | (latched_a9_ ? 0x1000U : 0U));
}

Byte NanjingFc001::read_chr(Address addr) {
    return chr_ram_[chr_address(addr)];
}

void NanjingFc001::write_chr(Address addr, Byte value) {
    chr_ram_[chr_address(addr)] = value;
}

MirrorMode NanjingFc001::mirror_mode() const {
    return mirror_;
}

bool NanjingFc001::maps_expansion(Address addr) const {
    const auto page = addr & 0xFF00U;
    return (page >= 0x5000U && page <= 0x5300U) || (addr & 0xF300U) == 0x5100U;
}

Byte NanjingFc001::read_expansion(Address addr) {
    return (addr & 0xF300U) == 0x5100U && !feedback_ ? 0x08U : 0U;
}

Byte NanjingFc001::expansion_read_mask(Address addr) const {
    return (addr & 0xF300U) == 0x5100U ? 0x08U : 0U;
}

void NanjingFc001::write_expansion(Address addr, Byte value) {
    switch (addr & 0xFF00U) {
    case 0x5000U:
        bank_low_ = register_value(value, mode_) & 0x8FU;
        break;
    case 0x5100U:
        if ((addr & 0xFEU) != 0U)
            break;
        value = register_value(value, mode_);
        if ((addr & 1U) == 0U)
            feedback_ = (value & 4U) != 0U;
        else if ((value & 1U) != 0U)
            feedback_ = !feedback_;
        break;
    case 0x5200U:
        bank_high_ = register_value(value, mode_) & 3U;
        break;
    case 0x5300U:
        mode_ = value & 5U;
        break;
    default:
        break;
    }
}

bool NanjingFc001::observes_ppu_address() const {
    return true;
}

void NanjingFc001::observe_ppu_address(Address addr) {
    const bool a13 = (addr & 0x2000U) != 0U;
    if (a13 && !previous_a13_)
        latched_a9_ = (addr & 0x0200U) != 0U;
    previous_a13_ = a13;
}

void NanjingFc001::reset() {
    bank_low_ = 0U;
    bank_high_ = 0U;
    mode_ = 0U;
    feedback_ = false;
    latched_a9_ = false;
    previous_a13_ = false;
}

void NanjingFc001::power_on() {
    reset();
    chr_ram_.fill(0U);
}

std::span<const Byte> NanjingFc001::battery_ram() const {
    return wram_;
}

void NanjingFc001::set_battery_ram(std::span<const Byte> data) {
    if (data.size() == wram_.size())
        std::ranges::copy(data, wram_.begin());
}

uint16_t NanjingFc001::state_tag() const {
    return static_cast<uint16_t>(kStateTag | (mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void NanjingFc001::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<uint32_t>(prg_rom_.size()));
    writer.write(bank_low_);
    writer.write(bank_high_);
    writer.write(mode_);
    writer.write(static_cast<Byte>(feedback_));
    writer.write(static_cast<Byte>(latched_a9_));
    writer.write(static_cast<Byte>(previous_a13_));
    writer.write_array(wram_);
    writer.write_array(chr_ram_);
}

void NanjingFc001::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_size = reader.read<uint32_t>();
    const auto bank_low = reader.read<Byte>();
    const auto bank_high = reader.read<Byte>();
    const auto mode = reader.read<Byte>();
    const auto feedback = reader.read<Byte>();
    const auto latched_a9 = reader.read<Byte>();
    const auto previous_a13 = reader.read<Byte>();
    std::array<Byte, 8192> wram{};
    std::array<Byte, 8192> chr{};
    reader.read_array(wram);
    reader.read_array(chr);
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        (bank_low & 0x70U) != 0U || bank_high > 3U || (mode & ~5U) != 0U || feedback > 1U ||
        latched_a9 > 1U || previous_a13 > 1U) {
        reader.fail();
        return;
    }
    bank_low_ = bank_low;
    bank_high_ = bank_high;
    mode_ = mode;
    feedback_ = feedback != 0U;
    latched_a9_ = latched_a9 != 0U;
    previous_a13_ = previous_a13 != 0U;
    wram_ = wram;
    chr_ram_ = chr;
}

} // namespace mapperbus::core
