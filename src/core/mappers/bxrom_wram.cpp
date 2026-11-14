#include "core/mappers/bxrom_wram.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/apu/expansion_gains.hpp"
#include "core/mappers/lpc_speech.hpp"
#include "core/state/state.hpp"

namespace mapperbus::core {

// Original implementation from NESdev mapper 241 r23728. The full-byte latch
// follows Mesen's published Mapper241 behavior; electrical populations vary.
BxromWram::BxromWram(const INesHeader& h, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_(std::move(chr_rom)), mirror_(h.mirror_mode),
      chr_ram_(chr_.empty()), battery_(h.has_battery), region_(h.region) {
    if (chr_ram_)
        chr_.resize(8192U);
}
BxromWram::~BxromWram() = default;
Result<void> BxromWram::configure_board_profile(std::string_view profile) {
    if (profile == "standard") {
        speech_.reset();
    } else if (profile == "lpc") {
        speech_ = std::make_unique<LpcSpeech>();
        speech_->set_region(region_);
    } else {
        return std::unexpected("Mapper 241 board profile must be standard or lpc");
    }
    return {};
}
bool BxromWram::maps_expansion(Address addr) const {
    return speech_ && addr >= 0x5000U && addr <= 0x5FFFU;
}
Byte BxromWram::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0x40U : 0U;
}
Byte BxromWram::read_expansion(Address addr) {
    return maps_expansion(addr) && speech_->ready() ? 0x40U : 0U;
}
void BxromWram::write_expansion(Address addr, Byte data) {
    if (!maps_expansion(addr))
        return;
    // NESdev defines both control pins high as data and both low as reset.
    // Mixed control levels are unspecified; leave the device alone.
    if ((data & 0x50U) == 0x50U)
        speech_->write_nibble(data);
    else if ((data & 0x50U) == 0U)
        speech_->reset();
}
bool BxromWram::has_expansion_audio() const {
    return speech_ != nullptr;
}
void BxromWram::clock_audio() {
    if (speech_)
        speech_->clock();
}
float BxromWram::audio_output() const {
    return speech_ ? static_cast<float>(speech_->output()) / 32768.0f * expansion_gains::kLpc
                   : 0.0f;
}
void BxromWram::set_region(Region region) {
    region_ = region;
    if (speech_)
        speech_->set_region(region);
}
Result<void> BxromWram::validate_geometry(const INesHeader& h,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (h.is_nes2 && h.submapper != 0U)
        return std::unexpected("Mapper 241 supports submapper 0");
    if (prg_size < 32768U || prg_size > 8388608U || !std::has_single_bit(prg_size) ||
        (chr_size != 0U && chr_size != 8192U))
        return std::unexpected(
            "Mapper 241 requires power-of-two 32 KiB-8 MiB PRG and 0/8 KiB CHR ROM");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 241 requires fixed H/V mirroring");
    if (h.is_nes2 && (h.prg_ram_size != (h.has_battery ? 0U : 8192U) ||
                      h.prg_nvram_size != (h.has_battery ? 8192U : 0U) ||
                      h.chr_ram_size != (chr_size == 0U ? 8192U : 0U) || h.chr_nvram_size != 0U))
        return std::unexpected(
            "Mapper 241 requires 8 KiB WRAM and fixed CHR ROM or 8 KiB volatile CHR RAM");
    return {};
}
Byte BxromWram::read_prg(Address addr) {
    if (addr >= 0x8000U) {
        const auto page = static_cast<std::size_t>(bank_) & (prg_rom_.size() / 32768U - 1U);
        return prg_rom_[page * 32768U + (addr & 0x7FFFU)];
    }
    return addr >= 0x6000U ? wram_[addr & 0x1FFFU] : 0U;
}
void BxromWram::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U)
        bank_ = value; // No bus conflicts; no overdump-specific D7 -> bank-8 hack.
    else if (addr >= 0x6000U)
        wram_[addr & 0x1FFFU] = value;
}
bool BxromWram::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}
Byte BxromWram::read_chr(Address addr) {
    return chr_[addr & 0x1FFFU];
}
void BxromWram::write_chr(Address addr, Byte value) {
    if (chr_ram_)
        chr_[addr & 0x1FFFU] = value;
}
MirrorMode BxromWram::mirror_mode() const {
    return mirror_;
}
void BxromWram::power_on() {
    reset();
    if (!battery_)
        wram_.fill(0U);
    if (chr_ram_)
        std::ranges::fill(chr_, 0U);
}
void BxromWram::reset() {
    bank_ = 0U; // Explicit compatibility policy; soft reset retains both RAMs.
    if (speech_)
        speech_->reset();
}
std::span<const Byte> BxromWram::battery_ram() const {
    return battery_ ? std::span<const Byte>{wram_} : std::span<const Byte>{};
}
void BxromWram::set_battery_ram(std::span<const Byte> data) {
    if (battery_)
        std::copy_n(data.begin(), std::min(data.size(), wram_.size()), wram_.begin());
}
uint16_t BxromWram::state_tag() const {
    const auto geometry = std::bit_width(prg_rom_.size() / 32768U) - 1U;
    return static_cast<uint16_t>(
        0xF100U | (geometry << 3U) | (mirror_ == MirrorMode::Vertical ? 4U : 0U) |
        (chr_ram_ ? 2U : 0U) | (battery_ ? 1U : 0U) | (speech_ ? 0x80U : 0U));
}
void BxromWram::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(bank_);
    writer.write_array(wram_);
    if (chr_ram_) {
        std::array<Byte, 8192> chr{};
        std::ranges::copy(chr_, chr.begin());
        writer.write_array(chr);
    }
    if (speech_)
        speech_->save_state(writer);
}
void BxromWram::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto bank = reader.read<Byte>();
    std::array<Byte, 8192> wram{}, chr{};
    reader.read_array(wram);
    if (chr_ram_)
        reader.read_array(chr);
    LpcSpeech speech;
    if (speech_) {
        speech = *speech_;
        speech.load_state(reader);
    }
    if (!reader.ok() || tag != state_tag()) {
        reader.fail();
        return;
    }
    if (speech_)
        *speech_ = speech;
    bank_ = bank;
    wram_ = wram;
    if (chr_ram_)
        std::ranges::copy(chr, chr_.begin());
}

} // namespace mapperbus::core
