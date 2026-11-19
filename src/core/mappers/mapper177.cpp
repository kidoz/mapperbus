#include "core/mappers/mapper177.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/apu/expansion_gains.hpp"
#include "core/mappers/lpc_speech.hpp"
#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 32U * 1024U;
constexpr std::size_t kWramBankSize = 8U * 1024U;
constexpr uint16_t kStateTag = 0xB100U;

bool valid_wram_size(std::size_t size) {
    return size == 8192U || size == 16384U || size == 32768U;
}

} // namespace

Henggedianzi177::Henggedianzi177(const INesHeader& header,
                                 std::vector<Byte> prg_rom,
                                 std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)),
      chr_ram_(header.is_nes2 && header.submapper == 1U ? 16384U : 8192U),
      wram_(header.is_nes2 ? header.prg_nvram_size : 8192U), mirror_(header.mirror_mode),
      submapper_(header.is_nes2 ? header.submapper : 0U), region_(header.region) {}

Henggedianzi177::~Henggedianzi177() = default;

Result<void> Henggedianzi177::validate_geometry(const INesHeader& header,
                                                std::size_t prg_size,
                                                std::size_t chr_size) {
    const auto submapper = header.is_nes2 ? header.submapper : 0U;
    if (submapper > 1U)
        return std::unexpected("Mapper 177 supports submappers 0 and 1");
    if (prg_size < 32768U || prg_size > 1048576U || !std::has_single_bit(prg_size) ||
        chr_size != 0U)
        return std::unexpected(
            "Mapper 177 requires power-of-two 32 KiB-1 MiB PRG ROM and no CHR ROM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 177 supports ordinary H/V CIRAM");
    if (!header.has_battery)
        return std::unexpected("Mapper 177 requires battery-backed PRG RAM");
    if (header.is_nes2 &&
        (header.prg_ram_size != 0U || !valid_wram_size(header.prg_nvram_size) ||
         header.chr_ram_size != (submapper == 1U ? 16384U : 8192U) || header.chr_nvram_size != 0U))
        return std::unexpected(
            "Mapper 177 requires 8-32 KiB PRG NVRAM and submapper-sized volatile CHR RAM");
    return {};
}

Result<void> Henggedianzi177::configure_board_profile(std::string_view profile) {
    if (profile == "standard") {
        speech_.reset();
    } else if (profile == "lpc") {
        speech_ = std::make_unique<LpcSpeech>();
        speech_->set_region(region_);
    } else {
        return std::unexpected("Mapper 177 board profile must be standard or lpc");
    }
    return {};
}

Byte Henggedianzi177::read_prg(Address addr) {
    if (addr >= 0x8000U) {
        const auto bank_mask = prg_rom_.size() / kPrgBankSize - 1U;
        const auto bank = static_cast<std::size_t>(latch_ & 0x1FU) & bank_mask;
        return prg_rom_[bank * kPrgBankSize + (addr & 0x7FFFU)];
    }
    if (addr >= 0x6000U) {
        const auto bank_mask = wram_.size() / kWramBankSize - 1U;
        const auto bank = static_cast<std::size_t>((latch_ >> 6U) & 3U) & bank_mask;
        return wram_[bank * kWramBankSize + (addr & 0x1FFFU)];
    }
    return 0U;
}

void Henggedianzi177::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U)
        latch_ = value;
    else if (addr >= 0x6000U)
        wram_[(((latch_ >> 6U) & 3U) & (wram_.size() / kWramBankSize - 1U)) * kWramBankSize +
              (addr & 0x1FFFU)] = value;
}

bool Henggedianzi177::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

bool Henggedianzi177::chr16_enabled() const {
    return submapper_ == 1U && (latch_ & 0x20U) != 0U;
}

Address Henggedianzi177::chr_address(Address addr) const {
    if (!chr16_enabled())
        return addr & 0x1FFFU;
    const auto bank = addr < 0x1000U ? nametable_chr_bank_ : 3U;
    return static_cast<Address>(bank * 0x1000U + (addr & 0x0FFFU));
}

Byte Henggedianzi177::read_chr(Address addr) {
    return chr_ram_[chr_address(addr)];
}

void Henggedianzi177::write_chr(Address addr, Byte value) {
    chr_ram_[chr_address(addr)] = value;
}

MirrorMode Henggedianzi177::mirror_mode() const {
    if (submapper_ == 0U)
        return (latch_ & 0x20U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    return mirror_;
}

bool Henggedianzi177::observes_ppu_address() const {
    return chr16_enabled();
}

void Henggedianzi177::observe_ppu_address(Address addr) {
    if (chr16_enabled() && addr >= 0x2000U && addr < 0x3F00U)
        nametable_chr_bank_ = static_cast<Byte>((addr >> 8U) & 3U);
}

bool Henggedianzi177::maps_expansion(Address addr) const {
    return speech_ && addr >= 0x5000U && addr <= 0x5FFFU;
}

Byte Henggedianzi177::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0x40U : 0U;
}

Byte Henggedianzi177::read_expansion(Address addr) {
    return maps_expansion(addr) && speech_->ready() ? 0x40U : 0U;
}

void Henggedianzi177::write_expansion(Address addr, Byte value) {
    if (!maps_expansion(addr))
        return;
    if ((value & 0x50U) == 0x50U)
        speech_->write_nibble(value);
    else if ((value & 0x50U) == 0U)
        speech_->reset();
}

bool Henggedianzi177::has_expansion_audio() const {
    return speech_ != nullptr;
}

void Henggedianzi177::set_region(Region region) {
    region_ = region;
    if (speech_)
        speech_->set_region(region);
}

void Henggedianzi177::clock_audio() {
    if (speech_)
        speech_->clock();
}

float Henggedianzi177::audio_output() const {
    return speech_ ? static_cast<float>(speech_->output()) / 32768.0F * expansion_gains::kLpc
                   : 0.0F;
}

void Henggedianzi177::reset() {
    // Reset behavior is not published; use deterministic zero latches and retain RAM.
    latch_ = 0U;
    nametable_chr_bank_ = 0U;
    if (speech_)
        speech_->reset();
}

void Henggedianzi177::power_on() {
    reset();
    std::ranges::fill(chr_ram_, 0U);
}

std::span<const Byte> Henggedianzi177::battery_ram() const {
    return wram_;
}

void Henggedianzi177::set_battery_ram(std::span<const Byte> data) {
    if (data.size() == wram_.size())
        std::ranges::copy(data, wram_.begin());
}

uint16_t Henggedianzi177::state_tag() const {
    const auto prg_geometry = std::bit_width(prg_rom_.size() / kPrgBankSize) - 1U;
    const auto wram_geometry = std::bit_width(wram_.size() / kWramBankSize) - 1U;
    return static_cast<uint16_t>(
        kStateTag | prg_geometry | (wram_geometry << 3U) | (submapper_ << 5U) |
        (mirror_ == MirrorMode::Vertical ? 0x40U : 0U) | (speech_ ? 0x80U : 0U));
}

void Henggedianzi177::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(latch_);
    writer.write(nametable_chr_bank_);
    writer.write_bytes(wram_);
    writer.write_bytes(chr_ram_);
    if (speech_)
        speech_->save_state(writer);
}

void Henggedianzi177::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto latch = reader.read<Byte>();
    const auto nametable_chr_bank = reader.read<Byte>();
    std::vector<Byte> wram, chr_ram;
    reader.read_bytes(wram, wram_.size());
    reader.read_bytes(chr_ram, chr_ram_.size());
    LpcSpeech speech;
    if (speech_) {
        speech = *speech_;
        speech.load_state(reader);
    }
    if (!reader.ok() || tag != state_tag() || nametable_chr_bank > 3U ||
        wram.size() != wram_.size() || chr_ram.size() != chr_ram_.size()) {
        reader.fail();
        return;
    }
    if (speech_)
        *speech_ = speech;
    latch_ = latch;
    nametable_chr_bank_ = nametable_chr_bank;
    wram_ = std::move(wram);
    chr_ram_ = std::move(chr_ram);
}

} // namespace mapperbus::core
