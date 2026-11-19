#include "core/mappers/konami_vrc.hpp"

#include <algorithm>
#include <bit>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
std::size_t ram_bytes(const INesHeader& header) {
    if (header.mapper_number == 75U)
        return 0U;
    if (header.mapper_number == 22U && !header.is_nes2)
        return 0U;
    return header.is_nes2 ? header.prg_ram_size + header.prg_nvram_size : 8192U;
}
} // namespace

// Specifications: NESdev VRC1, VRC2_and_VRC4, VRC3, VRC_IRQ.
// Waixing 252: r5906, VRC4 r24169 and IRQ r22720. Exact CHR banks 6/7
// select RAM before ROM wrapping. Clone-specific dynamic RAM relocation and
// propagation delays are unverified; retain the documented VRC4e digital model.
// 253: r5910 plus NRS's 2019 FS306 investigation. Default fixed CHR4/5;
// explicit f009s profile adds the observed $88/$C2/$C8 PPU-write commands.
// Cold $80/CHR28 startup is a documented inference from the related GAL board.
KonamiVrc::KonamiVrc(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_rom_(std::move(prg)), chr_rom_(std::move(chr)), prg_ram_(ram_bytes(header)),
      mapper_(header.mapper_number), submapper_(header.is_nes2 ? header.submapper : 0U),
      vrc2_(mapper_ == 22U || ((mapper_ == 23U || mapper_ == 25U) && submapper_ == 3U)),
      nvram_size_(header.is_nes2 ? header.prg_nvram_size
                                 : (header.has_battery ? prg_ram_.size() : 0U)),
      initial_mirror_(header.mirror_mode), mirror_(initial_mirror_) {
    reset();
}

Result<void> KonamiVrc::validate_fs306(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (h.mapper_number != 544 || !h.is_nes2 || h.submapper || !h.has_battery || h.has_trainer ||
        prg != 262144 || chr != 524288 || h.chr_ram_size != 2048 || h.prg_ram_size ||
        h.chr_nvram_size || h.prg_nvram_size != 8192 ||
        (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical))
        return std::unexpected(
            "FS306 requires 256/512 KiB PRG/CHR ROM, 8 KiB battery WRAM and 2 KiB CHR RAM");
    return {};
}

std::optional<Address> KonamiVrc::nametable_address(Address addr) const {
    if (mapper_ != 544U)
        return std::nullopt;
    return static_cast<Address>((fs306_ciram_[(addr >> 10U) & 3U] << 10U) | (addr & 0x3FFU));
}

Result<void> KonamiVrc::configure_board_profile(std::string_view profile) {
    if (mapper_ != 253U)
        return Mapper::configure_board_profile(profile);
    if (profile != "fixed" && profile != "f009s")
        return std::unexpected("Mapper 253 board profile must be fixed or f009s");
    waixing_dynamic_ = profile == "f009s";
    waixing_chr_mode_ = waixing_dynamic_ ? 0x80U : 0xC8U;
    return {};
}

void KonamiVrc::power_on() {
    reset();
    if (mapper_ == 252U || mapper_ == 253U || mapper_ == 544U) {
        if (mapper_ == 253U)
            waixing_chr_mode_ = waixing_dynamic_ ? 0x80U : 0xC8U;
        chr_ram_.fill(0U);
        if (nvram_size_ == 0U)
            std::ranges::fill(prg_ram_, 0U);
    }
}

void KonamiVrc::reset() {
    prg_banks_ = {0U, 1U, 0U};
    if (mapper_ == 544U) {
        waixing_chr_mode_ = 0x80U;
        fs306_ciram_ = {0, 0, 1, 1};
        prg_banks_[2] = 30U;
        fs306_irq_delay_ = 0;
    }
    chr_banks_.fill(0U);
    mirror_ = initial_mirror_;
    mode_ = microwire_ = irq_control_ = irq_enabled_ = irq_pending_ = 0U;
    irq_latch_ = irq_counter_ = 0U;
    irq_prescaler_ = 341U;
}

Byte KonamiVrc::port(Address address) const {
    const auto addr = static_cast<unsigned>(address);
    if (mapper_ == 544U)
        return (addr >> 10U) & 3U;
    if (mapper_ == 252U || mapper_ == 253U)
        return (addr >> 2U) & 3U;
    if (mapper_ == 21U) {
        if (submapper_ == 1U)
            return (addr >> 1U) & 3U;
        if (submapper_ == 2U)
            return (addr >> 6U) & 3U;
        return ((addr >> 1U) | (addr >> 6U)) & 3U;
    }
    if (mapper_ == 23U) {
        if (submapper_ == 2U)
            return (addr >> 2U) & 3U;
        if (submapper_ == 1U || submapper_ == 3U)
            return addr & 3U;
        return (addr | (addr >> 2U)) & 3U;
    }
    const auto normal = static_cast<Byte>(((addr >> 1U) & 1U) | ((addr & 1U) << 1U));
    if (mapper_ == 22U || submapper_ == 1U || submapper_ == 3U)
        return normal;
    const auto alternate = static_cast<Byte>(((addr >> 3U) & 1U) | ((addr >> 1U) & 2U));
    return submapper_ == 2U ? alternate : normal | alternate;
}

bool KonamiVrc::maps_prg(Address addr) const {
    if (addr >= 0x8000U)
        return true;
    if (addr < 0x6000U || mapper_ == 75U)
        return false;
    if (mapper_ == 73U || mapper_ == 544U)
        return !prg_ram_.empty();
    if (vrc2_)
        return !prg_ram_.empty() || addr < 0x7000U;
    return (mode_ & 1U) != 0U && !prg_ram_.empty() && (prg_ram_.size() >= 8192U || addr < 0x7000U);
}

Byte KonamiVrc::prg_read_mask(Address addr) const {
    return vrc2_ && prg_ram_.empty() && addr < 0x8000U ? 1U : 0xFFU;
}

Byte KonamiVrc::read_prg(Address addr) {
    if (addr < 0x8000U) {
        if (!maps_prg(addr))
            return 0U;
        if (prg_ram_.empty())
            return mapper_ == 23U ? microwire_ : 0U;
        return prg_ram_[(addr - 0x6000U) % prg_ram_.size()];
    }
    if (prg_rom_.empty())
        return 0U;
    if (mapper_ == 73U) {
        const std::size_t bank = addr < 0xC000U ? prg_banks_[0U] : prg_rom_.size() / 0x4000U - 1U;
        return prg_rom_[(bank * 0x4000U + (addr & 0x3FFFU)) % prg_rom_.size()];
    }
    const auto slot = (addr - 0x8000U) >> 13U;
    const std::size_t count = prg_rom_.size() / 0x2000U;
    std::size_t bank = count - 1U;
    if (mapper_ == 75U || mapper_ == 544U) {
        if (slot < 3U)
            bank = prg_banks_[slot];
    } else if (slot == 1U) {
        bank = prg_banks_[1U];
    } else if (slot != 3U) {
        const bool swap = !vrc2_ && (mode_ & 2U) != 0U;
        bank = (slot == 0U) != swap ? prg_banks_[0U] : count - 2U;
    }
    return prg_rom_[(bank * 0x2000U + (addr & 0x1FFFU)) % prg_rom_.size()];
}

void KonamiVrc::irq_control(Byte data) {
    const auto value = static_cast<unsigned>(data);
    irq_control_ = value & 7U;
    irq_enabled_ = (value >> 1U) & 1U;
    irq_pending_ = 0U;
    fs306_irq_delay_ = 0;
    if (mapper_ == 252U || mapper_ == 253U)
        irq_prescaler_ = 341U;
    if (irq_enabled_ != 0U) {
        irq_counter_ = irq_latch_;
        irq_prescaler_ = 341U;
    }
}

void KonamiVrc::irq_acknowledge() {
    irq_pending_ = 0U;
    fs306_irq_delay_ = 0;
    irq_enabled_ = irq_control_ & 1U;
}

void KonamiVrc::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (maps_prg(addr) && !prg_ram_.empty())
            prg_ram_[(addr - 0x6000U) % prg_ram_.size()] = value;
        else if (vrc2_ && addr < 0x7000U)
            microwire_ = value & 1U;
        return;
    }
    if (addr < 0x8000U)
        return;
    const auto group = static_cast<unsigned>(addr) >> 12U;
    if (mapper_ == 75U) {
        if (group == 8U || group == 10U || group == 12U)
            prg_banks_[(group - 8U) / 2U] = value & 15U;
        else if (group == 9U) {
            if (initial_mirror_ != MirrorMode::FourScreen)
                mirror_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
            chr_banks_[0U] = (chr_banks_[0U] & 15U) | ((value & 2U) << 3U);
            chr_banks_[1U] = (chr_banks_[1U] & 15U) | ((value & 4U) << 2U);
        } else if (group == 14U || group == 15U) {
            auto& bank = chr_banks_[group - 14U];
            bank = (bank & 16U) | (value & 15U);
        }
        return;
    }
    if (mapper_ == 73U) {
        if (group >= 8U && group <= 11U) {
            const auto shift = (group - 8U) * 4U;
            irq_latch_ =
                static_cast<uint16_t>((irq_latch_ & ~(0xFU << shift)) | ((value & 15U) << shift));
        } else if (group == 12U)
            irq_control(value);
        else if (group == 13U)
            irq_acknowledge();
        else if (group == 15U)
            prg_banks_[0U] = value & 7U;
        return;
    }
    const auto index = port(addr);
    if (group == 8U || group == 10U) {
        prg_banks_[group == 8U ? 0U : 1U] = value & 31U;
    } else if (group == 9U) {
        if (mapper_ == 544U) {
            if (index == 3U) {
                if ((addr & 4U) != 0)
                    fs306_ciram_[addr & 3U] = value & 1U;
                else
                    prg_banks_[2] = value & 31U;
            }
            return;
        }
        if (vrc2_ || index == 0U) {
            constexpr std::array kModes = {MirrorMode::Vertical,
                                           MirrorMode::Horizontal,
                                           MirrorMode::SingleLower,
                                           MirrorMode::SingleUpper};
            mirror_ = kModes[value & (vrc2_ ? 1U : 3U)];
        } else if (index == 2U)
            mode_ = value & 3U;
    } else if (group >= 11U && group <= 14U) {
        auto& bank = chr_banks_[(group - 11U) * 2U + (index >> 1U)];
        if ((index & 1U) == 0U)
            bank = (bank & 0x1F0U) | (value & 15U);
        else
            bank = (bank & 15U) | ((value & (vrc2_ ? 15U : 31U)) << 4U);
    } else if (group == 15U && !vrc2_) {
        switch (index) {
        case 0U:
            irq_latch_ = (irq_latch_ & 0xF0U) | (value & 15U);
            break;
        case 1U:
            irq_latch_ = (irq_latch_ & 15U) | ((value & 15U) << 4U);
            break;
        case 2U:
            irq_control(value);
            break;
        case 3U:
            irq_acknowledge();
            break;
        default:
            break;
        }
    }
}

std::size_t KonamiVrc::chr_offset(Address addr) const {
    if (mapper_ == 73U)
        return addr;
    if (mapper_ == 75U)
        return chr_banks_[addr >> 12U] * 0x1000U + (addr & 0x0FFFU);
    const auto bank = mapper_ == 22U ? chr_banks_[addr >> 10U] >> 1U : chr_banks_[addr >> 10U];
    return bank * 0x400U + (addr & 0x03FFU);
}
std::optional<std::size_t> KonamiVrc::waixing_chr_ram_offset(Address addr) const {
    const auto bank = chr_banks_[(addr >> 10U) & 7U];
    bool ram = false;
    if (mapper_ == 252U)
        ram = bank == 6U || bank == 7U;
    else if (mapper_ == 544U) {
        constexpr std::array<unsigned, 8> first{
            0x28U, 0x00U, 0x4CU, 0x64U, 0x46U, 0x7CU, 0x04U, 0xFFU};
        const auto code = ((waixing_chr_mode_ >> 1U) & 1U) | ((waixing_chr_mode_ >> 2U) & 2U) |
                          ((waixing_chr_mode_ >> 4U) & 4U);
        const unsigned mask = (waixing_chr_mode_ & 0x40U) ? 0xFEU : 0xFCU;
        ram = !(waixing_chr_mode_ & 0x10U) && (bank & mask) == first[code];
    } else if (!waixing_dynamic_)
        ram = bank == 4U || bank == 5U;
    else {
        const unsigned mask =
            waixing_chr_mode_ == 0x80U || waixing_chr_mode_ == 0x88U ? 0xFCU : 0xFEU;
        const unsigned first = waixing_chr_mode_ == 0x80U   ? 0x28U
                               : waixing_chr_mode_ == 0x88U ? 0x4CU
                               : waixing_chr_mode_ == 0xC2U ? 0x7CU
                                                            : 4U;
        ram = (bank & mask) == first;
    }
    // All decoder settings share the same 2 KiB chip; four-bank modes mirror it.
    return ram ? std::optional<std::size_t>{(bank & 1U) * 1024U + (addr & 0x3FFU)} : std::nullopt;
}
Byte KonamiVrc::read_chr(Address addr) {
    if (mapper_ == 252U || mapper_ == 253U || mapper_ == 544U)
        if (const auto ram = waixing_chr_ram_offset(addr))
            return chr_ram_[*ram];
    const auto offset = chr_offset(addr & 0x1FFFU);
    return chr_rom_.empty() ? chr_ram_[offset % chr_ram_.size()]
                            : chr_rom_[offset % chr_rom_.size()];
}
void KonamiVrc::write_chr(Address addr, Byte value) {
    if (mapper_ == 252U || mapper_ == 253U || mapper_ == 544U) {
        const auto bank = chr_banks_[(addr >> 10U) & 7U];
        if (mapper_ == 544U && (bank & 0x80U))
            waixing_chr_mode_ = bank & 0xDAU;
        if (mapper_ == 253U && waixing_dynamic_ &&
            (bank == 0x88U || bank == 0xC2U || bank == 0xC8U))
            waixing_chr_mode_ = static_cast<Byte>(bank);
        if (const auto ram = waixing_chr_ram_offset(addr))
            chr_ram_[*ram] = value;
        return;
    }
    if (chr_rom_.empty())
        chr_ram_[chr_offset(addr & 0x1FFFU) % chr_ram_.size()] = value;
}
MirrorMode KonamiVrc::mirror_mode() const {
    return mirror_;
}

void KonamiVrc::irq_tick() {
    const uint32_t mask = mapper_ == 73U && (irq_control_ & 4U) == 0U ? 0xFFFFU : 0x00FFU;
    if ((irq_counter_ & mask) == mask) {
        irq_counter_ = static_cast<uint16_t>((irq_counter_ & ~mask) | (irq_latch_ & mask));
        if (mapper_ == 544U)
            fs306_irq_delay_ = 1;
        else
            irq_pending_ = 1U;
    } else
        ++irq_counter_;
}
void KonamiVrc::clock_cpu() {
    if (fs306_irq_delay_) {
        irq_pending_ = 1;
        fs306_irq_delay_ = 0;
    }
    if (vrc2_ || mapper_ == 75U || irq_enabled_ == 0U)
        return;
    if (mapper_ == 73U || (irq_control_ & 4U) != 0U)
        irq_tick();
    else if (irq_prescaler_ <= 3U) {
        irq_prescaler_ += 338U;
        irq_tick();
    } else
        irq_prescaler_ -= 3U;
}
bool KonamiVrc::irq_pending() const {
    return irq_pending_ != 0U;
}
void KonamiVrc::acknowledge_irq() {
    irq_pending_ = 0U;
}

void KonamiVrc::save_state(StateWriter& w) const {
    if (mapper_ == 252U || mapper_ == 253U || mapper_ == 544U) {
        save_waixing_state(w);
        return;
    }
    w.write_array(prg_banks_);
    w.write_array(chr_banks_);
    w.write(mirror_);
    w.write(mode_);
    w.write(microwire_);
    w.write(irq_latch_);
    w.write(irq_counter_);
    w.write(irq_prescaler_);
    w.write(irq_control_);
    w.write(irq_enabled_);
    w.write(irq_pending_);
    w.write_bytes(prg_ram_);
    w.write_array(chr_ram_);
}
void KonamiVrc::load_state(StateReader& r) {
    if (mapper_ == 252U || mapper_ == 253U || mapper_ == 544U) {
        load_waixing_state(r);
        return;
    }
    r.read_array(prg_banks_);
    r.read_array(chr_banks_);
    r.read(mirror_);
    r.read(mode_);
    r.read(microwire_);
    r.read(irq_latch_);
    r.read(irq_counter_);
    r.read(irq_prescaler_);
    r.read(irq_control_);
    r.read(irq_enabled_);
    r.read(irq_pending_);
    const auto expected = prg_ram_.size();
    r.read_bytes(prg_ram_, expected);
    r.read_array(chr_ram_);
    if (prg_ram_.size() != expected || mirror_ > MirrorMode::SingleUpper || mode_ > 3U ||
        microwire_ > 1U || irq_control_ > 7U || irq_enabled_ > 1U || irq_pending_ > 1U ||
        irq_prescaler_ == 0U || irq_prescaler_ > 341U ||
        std::ranges::any_of(chr_banks_, [](uint16_t bank) { return bank > 511U; })) {
        prg_ram_.resize(expected);
        reset();
        r.fail();
    }
}
uint16_t KonamiVrc::waixing_state_tag() const {
    const unsigned wiring =
        (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U) | (nvram_size_ != 0U ? 2U : 0U);
    if (mapper_ == 544U)
        return static_cast<Word>(0x5440U | wiring);
    if (mapper_ == 252U)
        return static_cast<uint16_t>(0xFC00U | wiring);
    const auto prg = std::bit_width(prg_rom_.size() / 32768U) - 1U;
    const auto chr = std::bit_width(chr_rom_.size() / 8192U) - 1U;
    return static_cast<uint16_t>(0xFD00U | wiring | (prg << 2U) | (chr << 4U) |
                                 (waixing_dynamic_ ? 0x80U : 0U));
}
void KonamiVrc::save_waixing_state(StateWriter& w) const {
    w.write(waixing_state_tag());
    w.write(prg_banks_[0U]);
    w.write(prg_banks_[1U]);
    w.write_array(chr_banks_);
    w.write(mirror_);
    w.write(mode_);
    w.write(static_cast<Byte>(irq_latch_));
    w.write(static_cast<Byte>(irq_counter_));
    w.write(irq_prescaler_);
    w.write(irq_control_);
    w.write(irq_enabled_);
    w.write(irq_pending_);
    if (mapper_ == 253U || mapper_ == 544U)
        w.write(waixing_chr_mode_);
    if (mapper_ == 544U) {
        w.write(prg_banks_[2]);
        w.write_array(fs306_ciram_);
        w.write(fs306_irq_delay_);
    }
    for (const auto value : prg_ram_)
        w.write(value);
    for (const auto value : std::span(chr_ram_).first(2048U))
        w.write(value);
}
void KonamiVrc::load_waixing_state(StateReader& r) {
    const auto tag = r.read<uint16_t>();
    std::array<Byte, 2> prg{};
    std::array<uint16_t, 8> chr{};
    r.read_array(prg);
    r.read_array(chr);
    const auto mirror = r.read<MirrorMode>();
    const auto mode = r.read<Byte>();
    const auto latch = r.read<Byte>(), counter = r.read<Byte>();
    const auto prescaler = r.read<uint16_t>();
    const auto control = r.read<Byte>(), enabled = r.read<Byte>(), pending = r.read<Byte>();
    const auto chr_mode = mapper_ == 253U || mapper_ == 544U ? r.read<Byte>() : Byte{0xC8U};
    Byte third_bank = 0, delay = 0;
    decltype(fs306_ciram_) ciram{};
    if (mapper_ == 544U) {
        third_bank = r.read<Byte>();
        r.read_array(ciram);
        delay = r.read<Byte>();
        if (third_bank > 31 || delay > enabled ||
            std::ranges::any_of(ciram, [](Byte bit) { return bit > 1; }))
            r.fail();
    }
    const bool valid_chr_mode =
        mapper_ == 544U
            ? ((chr_mode & 0xA5U) == 0x80U)
            : chr_mode == 0xC8U || (waixing_dynamic_ &&
                                    (chr_mode == 0x80U || chr_mode == 0x88U || chr_mode == 0xC2U));
    std::array<Byte, 8192> wram{};
    std::array<Byte, 2048> cram{};
    r.read_array(wram);
    r.read_array(cram);
    if (!r.ok() || !valid_chr_mode || tag != waixing_state_tag() || prg[0U] > 31U ||
        prg[1U] > 31U || std::ranges::any_of(chr, [](uint16_t bank) { return bank > 511U; }) ||
        mirror > MirrorMode::SingleUpper || mirror == MirrorMode::FourScreen || mode > 3U ||
        prescaler == 0U || prescaler > 341U || control > 7U || enabled > 1U || pending > 1U ||
        (pending != 0U && enabled == 0U)) {
        r.fail();
        return;
    }
    prg_banks_ = {prg[0U], prg[1U], third_bank};
    fs306_ciram_ = ciram;
    fs306_irq_delay_ = delay;
    chr_banks_ = chr;
    waixing_chr_mode_ = chr_mode;
    mirror_ = mirror;
    mode_ = mode;
    microwire_ = 0U;
    irq_latch_ = latch;
    irq_counter_ = counter;
    irq_prescaler_ = prescaler;
    irq_control_ = control;
    irq_enabled_ = enabled;
    irq_pending_ = pending;
    std::ranges::copy(wram, prg_ram_.begin());
    std::ranges::copy(cram, chr_ram_.begin());
}
std::span<const Byte> KonamiVrc::battery_ram() const {
    return std::span<const Byte>(prg_ram_).last(std::min(nvram_size_, prg_ram_.size()));
}
void KonamiVrc::set_battery_ram(std::span<const Byte> data) {
    const auto count = std::min(nvram_size_, prg_ram_.size());
    std::copy_n(data.begin(), std::min(data.size(), count), prg_ram_.end() - count);
}

} // namespace mapperbus::core
