#include "core/mappers/mapper594.hpp"

#include <algorithm>
#include <utility>

#include "core/apu/expansion_gains.hpp"
#include "core/state/state.hpp"
namespace mapperbus::core {
Mapper594::Mapper594(const INesHeader& h, std::vector<Byte> prg, std::vector<Byte> chr)
    : Mmc3(h, std::move(prg), std::move(chr)) {
    set_region(h.region);
}
Result<void> Mapper594::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (h.mapper_number != 594 || !h.is_nes2 || h.submapper || h.has_battery || h.has_trainer ||
        prg != 3 * 1024 * 1024 || chr != 1024 * 1024 || h.prg_ram_size || h.prg_nvram_size ||
        h.chr_nvram_size || h.chr_ram_size ||
        (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical))
        return std::unexpected("Rinco Talkman requires 3 MiB PRG ROM and 1 MiB CHR ROM");
    return {};
}
bool Mapper594::maps_prg(Address addr) const {
    return addr >= 0x6000;
}
Byte Mapper594::read_prg(Address addr) {
    if (addr < 0x6000)
        return 0;
    const unsigned base = mode_ & 2U ? 0x200000U : mode_ == 1 ? 0x180000U : 0U;
    const unsigned mask = addr >= 0x8000 || mode_ == 1 ? 63U : (mode_ & 2U) ? 127U : 255U;
    const unsigned bank = (addr < 0x8000 ? extra_ : prg_bank(addr)) & mask;
    return physical_prg(base + bank * 8192U + (addr & 8191U));
}
void Mapper594::write_prg(Address addr, Byte value) {
    if ((addr & 0xF000U) == 0x9000) {
        if (!(addr & 1U))
            extra_ = value;
    } else if ((addr & 0xF000U) == 0xB000)
        mode_ = value >> 6U;
    else if (addr >= 0x8000U)
        Mmc3::write_prg(addr, value);
}
Byte Mapper594::read_chr(Address addr) {
    const unsigned block = mode_ == 0 ? ((addr >> 12U) & 1U) : (mode_ == 1 ? 2U : 3U);
    return physical_chr((block << 18U) | (chr_bank(addr) * 1024U) | (addr & 1023U));
}
bool Mapper594::maps_expansion(Address addr) const {
    return (addr & 0xF001U) == 0x5001;
}
Byte Mapper594::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0x40U : 0U;
}
Byte Mapper594::read_expansion(Address addr) {
    return maps_expansion(addr) && count_ < 512 ? 0x40U : 0U;
}
void Mapper594::write_expansion(Address addr, Byte value) {
    if ((addr & 0xF000U) != 0x5000U)
        return;
    if (addr & 1U) {
        head_ = count_ = 0;
        low_nibble_ = false;
        rate_ = value >> 6U;
        phase_ = 0;
    } else if (count_ < fifo_.size()) {
        fifo_[(head_ + count_) & 1023U] = value;
        ++count_;
    }
}
bool Mapper594::has_expansion_audio() const {
    return true;
}
void Mapper594::set_region(Region region) {
    cpu_hz_ = static_cast<std::uint32_t>(timing_for_region(region).cpu_clock_hz() + 0.5);
    phase_ %= cpu_hz_;
}
void Mapper594::decode_nibble(Byte nibble) {
    // OKI four-bit ADPCM quantizer with the MSM6585's saturating 12-bit DAC.
    constexpr std::array<int, 49> steps{
        16,  17,  19,  21,  23,  25,  28,  31,  34,  37,  41,   45,   50,   55,   60,  66,  73,
        80,  88,  97,  107, 118, 130, 143, 157, 173, 190, 209,  230,  253,  279,  307, 337, 371,
        408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552};
    constexpr std::array<int, 8> adjust{-1, -1, -1, -1, 2, 4, 6, 8};
    const int step = steps[step_index_];
    const int delta = (step >> 3) + ((nibble & 1U) ? step >> 2 : 0) +
                      ((nibble & 2U) ? step >> 1 : 0) + ((nibble & 4U) ? step : 0);
    sample_ = static_cast<std::int16_t>(
        std::clamp(sample_ + ((nibble & 8U) ? -delta : delta), -2048, 2047));
    step_index_ = std::clamp(step_index_ + adjust[nibble & 7U], 0, 48);
}
void Mapper594::clock_audio() {
    phase_ += 4000U << rate_;
    if (phase_ < cpu_hz_)
        return;
    phase_ -= cpu_hz_;
    if (!count_)
        return;
    const Byte data = fifo_[head_];
    decode_nibble(low_nibble_ ? data & 15U : data >> 4U);
    if (low_nibble_) {
        head_ = (head_ + 1U) & 1023U;
        --count_;
    }
    low_nibble_ = !low_nibble_;
}
float Mapper594::audio_output() const {
    return (sample_ / 2048.0F) * expansion_gains::kTalkman;
}
void Mapper594::reset() {
    Mmc3::reset();
    extra_ = mode_ = rate_ = step_index_ = 0;
    head_ = count_ = 0;
    low_nibble_ = false;
    sample_ = 0;
    phase_ = 0;
    fifo_.fill(0);
}
void Mapper594::save_state(StateWriter& w) const {
    w.write<Word>(594);
    w.write(extra_);
    w.write(mode_);
    w.write(rate_);
    w.write(step_index_);
    w.write(head_);
    w.write(count_);
    w.write<Byte>(low_nibble_);
    w.write(sample_);
    w.write(phase_);
    w.write_array(fifo_);
    Mmc3::save_state(w);
}
void Mapper594::load_state(StateReader& r) {
    auto tag = r.read<Word>();
    auto extra = r.read<Byte>();
    auto mode = r.read<Byte>();
    auto rate = r.read<Byte>();
    auto step = r.read<Byte>();
    auto head = r.read<Word>();
    auto count = r.read<Word>();
    auto low = r.read<Byte>();
    auto sample = r.read<std::int16_t>();
    auto phase = r.read<std::uint32_t>();
    decltype(fifo_) fifo{};
    r.read_array(fifo);
    if (!r.ok() || tag != 594 || mode > 3 || rate > 3 || step > 48 || head >= 1024 ||
        count > 1024 || low > 1 || (!count && low) || sample < -2048 || sample > 2047 ||
        phase >= cpu_hz_) {
        r.fail();
        return;
    }
    Mmc3::load_state(r);
    if (r.ok()) {
        extra_ = extra;
        mode_ = mode;
        rate_ = rate;
        step_index_ = step;
        head_ = head;
        count_ = count;
        low_nibble_ = low != 0;
        sample_ = sample;
        phase_ = phase;
        fifo_ = fifo;
    }
}
} // namespace mapperbus::core
