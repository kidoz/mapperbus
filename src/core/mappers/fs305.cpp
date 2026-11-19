#include "core/mappers/fs305.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/apu/expansion_gains.hpp"
#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t kPage = 8192U;
constexpr std::size_t kPrgBank = 16384U;
} // namespace

Fs305::Fs305(const INesHeader& h, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_(std::move(chr_rom)),
      wram_(h.prg_ram_size + h.prg_nvram_size), mapper_id_(h.mapper_number),
      submapper_(h.submapper), fixed_mirror_(h.mirror_mode), battery_(h.has_battery) {
    if (mapper_id_ == 178U)
        chr_.resize(kPage);
    if (submapper_ == 2U) {
        speech_.emplace();
        set_region(h.region);
    }
    power_on();
}

Result<void> Fs305::validate_geometry(const INesHeader& h,
                                      std::size_t prg_size,
                                      std::size_t chr_size) {
    if (h.mapper_number == 551U) {
        if (!h.is_nes2 || h.submapper != 0U || chr_size < kPage || chr_size > 256U * kPage ||
            !std::has_single_bit(chr_size) || h.chr_ram_size != 0U ||
            (h.has_battery ? h.prg_nvram_size : h.prg_ram_size) != kPage)
            return std::unexpected(
                "Mapper 551 requires submapper 0, banked CHR ROM and 8 KiB WRAM");
    } else if (h.mapper_number == 178U) {
        if (h.submapper == 1U || h.submapper > 3U)
            return std::unexpected(
                "Mapper 178 supports the standard, speech and solder-pad boards");
        if (chr_size != 0U || h.chr_ram_size != kPage)
            return std::unexpected("Mapper 178 requires 8 KiB of volatile CHR RAM");
    } else {
        return std::unexpected("FS305 requires mapper 178 or 551");
    }
    if (prg_size < 2U * kPrgBank || prg_size > 2048U * kPrgBank || !std::has_single_bit(prg_size))
        return std::unexpected("Unsupported FS305 PRG ROM size");
    const auto ram = h.has_battery ? h.prg_nvram_size : h.prg_ram_size;
    if ((h.has_battery ? h.prg_ram_size : h.prg_nvram_size) != 0U || h.chr_nvram_size != 0U ||
        (h.has_battery && ram == 0U) || (h.has_trainer && ram < kPage) ||
        (ram != 0U && (ram < kPage || ram > 256U * kPage || !std::has_single_bit(ram))))
        return std::unexpected("Unsupported FS305 RAM population");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("FS305 requires horizontal or vertical CIRAM");
    return {};
}

std::size_t Fs305::ram_address(Address addr) const {
    const auto page = mapper_id_ == 178U ? regs_[3] : 0U;
    return (page * kPage + (addr & 0x1FFFU)) & (wram_.size() - 1U);
}

Byte Fs305::read_prg(Address addr) {
    if (addr < 0x8000U)
        return maps_prg(addr) ? wram_[ram_address(addr)] : 0U;
    if (read_jumpers_)
        return jumpers_;
    unsigned inner = regs_[1];
    const unsigned half = (addr >> 14U) & 1U;
    switch (regs_[0] >> 1U) {
    case 0:
        inner = (inner & 6U) | half;
        break;
    case 1:
        if (half != 0U)
            inner = 7U;
        break;
    case 3:
        if (half != 0U)
            inner |= 6U;
        break;
    default:
        break; // Identical 16 KiB bank in both windows.
    }
    const unsigned bank = (static_cast<unsigned>(regs_[2]) << 3U) | inner;
    return prg_rom_[(bank * kPrgBank + (addr & 0x3FFFU)) & (prg_rom_.size() - 1U)];
}

void Fs305::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (submapper_ == 3U)
            read_jumpers_ = (value & 1U) != 0U;
        if (!wram_.empty())
            wram_[ram_address(addr)] = value;
    }
}

bool Fs305::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && !wram_.empty());
}

Byte Fs305::read_chr(Address addr) {
    const auto page = mapper_id_ == 551U ? regs_[3] : 0U;
    return chr_[(page * kPage + (addr & 0x1FFFU)) & (chr_.size() - 1U)];
}
void Fs305::write_chr(Address addr, Byte value) {
    if (mapper_id_ == 178U)
        chr_[addr & 0x1FFFU] = value;
}
MirrorMode Fs305::mirror_mode() const {
    if (mapper_id_ == 551U)
        return fixed_mirror_;
    return (regs_[0] & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}
bool Fs305::maps_expansion(Address addr) const {
    return speech_ && addr >= 0x5000U && addr < 0x6000U;
}
Byte Fs305::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0x40U : 0U;
}
Byte Fs305::read_expansion(Address addr) {
    return maps_expansion(addr) && speech_->ready() ? 0x40U : 0U;
}

void Fs305::write_expansion(Address addr, Byte value) {
    if (addr >= 0x4800U && addr < 0x5000U) {
        const auto index = addr & 3U;
        regs_[index] = index < 2U ? (value & 7U) : value;
    } else if (maps_expansion(addr))
        speech_->write_control(value);
}

bool Fs305::has_expansion_audio() const {
    return speech_.has_value();
}
void Fs305::clock_audio() {
    if (speech_)
        speech_->clock();
}
float Fs305::audio_output() const {
    return speech_ ? static_cast<float>(speech_->output()) / 32768.0f * expansion_gains::kLpc
                   : 0.0f;
}
void Fs305::set_region(Region region) {
    if (speech_)
        speech_->set_region(region);
}

Result<void> Fs305::configure_board_jumpers(Byte value) {
    if (submapper_ != 3U || value > 3U)
        return std::unexpected("Only mapper 178 submapper 3 accepts a two-bit solder pad");
    jumpers_ = value;
    return {};
}
std::span<Byte> Fs305::trainer_ram() {
    return wram_.empty() ? std::span<Byte>{} : std::span{wram_}.subspan(0x1000U, 0x1000U);
}
std::span<const Byte> Fs305::battery_ram() const {
    return battery_ ? std::span<const Byte>(wram_) : std::span<const Byte>{};
}
void Fs305::set_battery_ram(std::span<const Byte> data) {
    if (battery_ && data.size() == wram_.size())
        std::ranges::copy(data, wram_.begin());
}
void Fs305::reset() {
    regs_.fill(0U);
    read_jumpers_ = false;
    if (speech_)
        speech_->reset();
}
void Fs305::power_on() {
    reset();
    if (!battery_)
        std::ranges::fill(wram_, 0U);
    if (mapper_id_ == 178U)
        std::ranges::fill(chr_, 0U);
}

void Fs305::save_state(StateWriter& w) const {
    w.write(mapper_id_);
    w.write(submapper_);
    w.write(static_cast<Byte>(fixed_mirror_));
    w.write(static_cast<Byte>(battery_));
    w.write(static_cast<uint32_t>(prg_rom_.size()));
    w.write(static_cast<uint32_t>(chr_.size()));
    w.write(jumpers_);
    w.write(static_cast<Byte>(read_jumpers_));
    w.write_array(regs_);
    w.write_bytes(wram_);
    if (mapper_id_ == 178U)
        w.write_bytes(chr_);
    if (speech_)
        speech_->save_state(w);
}
void Fs305::load_state(StateReader& r) {
    if (r.read<uint16_t>() != mapper_id_ || r.read<Byte>() != submapper_ ||
        r.read<Byte>() != static_cast<Byte>(fixed_mirror_) ||
        r.read<Byte>() != static_cast<Byte>(battery_) || r.read<uint32_t>() != prg_rom_.size() ||
        r.read<uint32_t>() != chr_.size() || r.read<Byte>() != jumpers_) {
        r.fail();
        return;
    }
    const auto pads = r.read<Byte>();
    std::array<Byte, 4> regs{};
    r.read_array(regs);
    std::vector<Byte> ram, chr;
    r.read_bytes(ram, wram_.size());
    if (mapper_id_ == 178U)
        r.read_bytes(chr, kPage);
    auto speech = speech_;
    if (speech)
        speech->load_state(r);
    if (!r.ok() || pads > (submapper_ == 3U ? 1U : 0U) || regs[0] > 7U || regs[1] > 7U ||
        ram.size() != wram_.size() || (mapper_id_ == 178U && chr.size() != kPage)) {
        r.fail();
        return;
    }
    read_jumpers_ = pads != 0U;
    regs_ = regs;
    wram_ = std::move(ram);
    if (mapper_id_ == 178U)
        chr_ = std::move(chr);
    speech_ = speech;
}
} // namespace mapperbus::core
