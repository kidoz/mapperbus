#include "core/mappers/lpc_speech.hpp"

#include <algorithm>
#include <cmath>

#include "core/mappers/lpc_tables.hpp"
#include "core/state/state.hpp"

namespace mapperbus::core {
void LpcSpeech::set_region(Region region) {
    cpu_clock_ = static_cast<uint32_t>(std::lround(timing_for_region(region).cpu_clock_hz()));
    phase_ %= cpu_clock_;
}
void LpcSpeech::reset() {
    const auto cpu_clock = cpu_clock_;
    *this = LpcSpeech{};
    cpu_clock_ = cpu_clock;
}
void LpcSpeech::write_control(Byte data) {
    // NESdev defines both control pins high as data and both low as reset.
    // Mixed control levels are unspecified; leave the device alone.
    if ((data & 0x50U) == 0x50U)
        write_nibble(data);
    else if ((data & 0x50U) == 0U)
        reset();
}

void LpcSpeech::write_nibble(Byte data) {
    // Writes while !ready still fit until the actual 128-bit FIFO is full.
    // Overflow drops the complete nibble, without corrupting queued data.
    if (count_ > 124U)
        return;
    for (unsigned bit = 0; bit < 4U; ++bit) {
        const unsigned index = (head_ + count_) & 127U;
        const auto mask = static_cast<Byte>(1U << (index & 7U));
        fifo_[index / 8U] = static_cast<Byte>((fifo_[index / 8U] & ~mask) |
                                              (((data >> bit) & 1U) != 0U ? mask : 0U));
        ++count_;
    }
}
bool LpcSpeech::ready() const {
    return reset_clocks_ == 0U && count_ < 64U;
}
unsigned LpcSpeech::read_bits(unsigned width) {
    unsigned result = 0;
    for (unsigned i = 0; i < width; ++i) {
        result = (result << 1U) | ((fifo_[head_ / 8U] >> (head_ & 7U)) & 1U);
        head_ = static_cast<Byte>((head_ + 1U) & 127U);
        --count_;
    }
    return result;
}
void LpcSpeech::parse() {
    // A field consumes bits only when complete. Partial frames may span any
    // number of FIFO refills; coefficients are committed only on frame completion.
    while (!playing_) {
        const unsigned width = field_ == 0U   ? 4U
                               : field_ == 1U ? 1U
                               : field_ == 2U ? 6U
                                              : lpc_tables::kWidths[field_ - 3U];
        if (count_ < width)
            return;
        const unsigned value = read_bits(width);
        if (field_ == 0U) {
            pending_ = target_;
            pending_.energy = lpc_tables::kEnergy[value];
            if (value == 15U) {
                // A stop code ends the message; discard padding to the next
                // byte boundary. A queued following message remains available.
                (void)read_bits(count_ & 7U);
                current_ = target_ = pending_ = Frame{};
                delay_.fill(0);
                output_ = 0;
                pitch_count_ = 0U;
                repeat_ = false;
                interpolate_ = false;
                continue;
            }
            if (value == 0U) {
                pending_.pitch = 0;
                start_frame();
                return;
            }
        } else if (field_ == 1U) {
            repeat_ = value != 0U;
        } else if (field_ == 2U) {
            pending_.pitch = lpc_tables::kPitch[value];
            if (repeat_) {
                start_frame();
                return;
            }
        } else {
            pending_.k[field_ - 3U] = lpc_tables::kReflection[field_ - 3U][value];
            if (field_ == 12U || (field_ == 6U && pending_.pitch == 0)) {
                start_frame();
                return;
            }
        }
        ++field_;
    }
}
void LpcSpeech::start_frame() {
    interpolate_ = target_.energy != 0 && ((target_.pitch == 0) == (pending_.pitch == 0)) &&
                   !(target_.pitch == 0 && pending_.energy == 0);
    target_ = pending_;
    if (!interpolate_) {
        current_ = target_;
        pitch_count_ = 0U;
    }
    // Keep raw K5-K10 for a later repeat frame, while removing their contribution
    // to an unvoiced frame's four-stage filter.
    if (target_.pitch == 0)
        std::fill(current_.k.begin() + 4, current_.k.end(), 0);
    frame_sample_ = 0U;
    field_ = 0U;
    playing_ = true;
}
int16_t LpcSpeech::wrap(int32_t value) {
    const auto bits = static_cast<uint32_t>(value) & 0x7FFFU;
    return static_cast<int16_t>(static_cast<int32_t>(bits) - ((bits & 0x4000U) != 0U ? 32768 : 0));
}
void LpcSpeech::sample() {
    if (!playing_)
        parse();
    if (!playing_) {
        output_ = 0; // Starved between frames: silence until more data arrives.
        return;
    }
    // Eight 25-sample interpolation periods. Parameter updates are simultaneous
    // here, rather than staggered over the TI chip's individual arithmetic cycles.
    if (interpolate_ && frame_sample_ % 25U == 0U) {
        const unsigned shift = lpc_tables::kInterpolation[frame_sample_ / 25U];
        current_.energy += static_cast<int16_t>((target_.energy - current_.energy) >> shift);
        current_.pitch += static_cast<int16_t>((target_.pitch - current_.pitch) >> shift);
        for (unsigned i = 0; i < 10U; ++i) {
            const auto k = target_.pitch == 0 && i >= 4U ? 0 : target_.k[i];
            current_.k[i] += static_cast<int16_t>((k - current_.k[i]) >> shift);
        }
    }
    const int excitation = target_.pitch == 0
                               ? ((noise_ & 1U) != 0U ? -64 : 64)
                               : lpc_tables::kChirp[std::min<unsigned>(pitch_count_, 51U)];
    for (unsigned i = 0; i < 20U; ++i) {
        const auto feedback = ((noise_ >> 12U) ^ (noise_ >> 3U) ^ (noise_ >> 2U) ^ noise_) & 1U;
        noise_ = static_cast<uint16_t>(((noise_ << 1U) | feedback) & 0x1FFFU);
    }
    std::array<int16_t, 11> forward{};
    forward[10] = wrap(current_.energy * excitation / 8);
    for (unsigned i = 10U; i > 0U; --i)
        forward[i - 1U] = wrap(forward[i] - ((current_.k[i - 1U] * delay_[i - 1U]) >> 9));
    for (unsigned i = 9U; i > 0U; --i)
        delay_[i] = wrap(delay_[i - 1U] + ((current_.k[i - 1U] * forward[i - 1U]) >> 9));
    delay_[0] = forward[0];
    output_ = static_cast<int16_t>((std::clamp<int>(forward[0], -2048, 2047) >> 4) * 256);
    ++pitch_count_;
    if (pitch_count_ >= std::max<int>(current_.pitch, 1))
        pitch_count_ = 0U;
    if (++frame_sample_ == 200U) {
        frame_sample_ = 0U;
        playing_ = false;
    }
}
void LpcSpeech::clock() {
    phase_ += 768000U;
    if (phase_ < cpu_clock_)
        return;
    phase_ -= cpu_clock_;
    if (reset_clocks_ != 0U) {
        --reset_clocks_;
        return;
    }
    if (++divider_ == 80U) {
        divider_ = 0U;
        sample();
    }
}
void LpcSpeech::save_frame(StateWriter& w, const Frame& f) {
    w.write(f.energy);
    w.write(f.pitch);
    w.write_array(f.k);
}
void LpcSpeech::load_frame(StateReader& r, Frame& f) {
    r.read(f.energy);
    r.read(f.pitch);
    r.read_array(f.k);
}
bool LpcSpeech::valid_frame(const Frame& f) {
    return f.energy >= 0 && f.energy <= 114 && f.pitch >= 0 && f.pitch <= 159 &&
           std::ranges::all_of(f.k, [](int16_t k) { return k >= -512 && k <= 511; });
}
void LpcSpeech::save_state(StateWriter& w) const {
    w.write<uint16_t>(0x4C01U);
    w.write_array(fifo_);
    w.write(head_);
    w.write(count_);
    w.write(phase_);
    w.write(reset_clocks_);
    w.write(divider_);
    w.write(frame_sample_);
    w.write(field_);
    w.write<Byte>(repeat_);
    w.write<Byte>(playing_);
    w.write<Byte>(interpolate_);
    w.write(pitch_count_);
    w.write(noise_);
    w.write(output_);
    save_frame(w, current_);
    save_frame(w, target_);
    save_frame(w, pending_);
    w.write_array(delay_);
}
void LpcSpeech::load_state(StateReader& r) {
    auto s = *this;
    const auto tag = r.read<uint16_t>();
    r.read_array(s.fifo_);
    r.read(s.head_);
    r.read(s.count_);
    r.read(s.phase_);
    r.read(s.reset_clocks_);
    r.read(s.divider_);
    r.read(s.frame_sample_);
    r.read(s.field_);
    const auto repeat = r.read<Byte>(), playing = r.read<Byte>(), interpolate = r.read<Byte>();
    r.read(s.pitch_count_);
    r.read(s.noise_);
    r.read(s.output_);
    load_frame(r, s.current_);
    load_frame(r, s.target_);
    load_frame(r, s.pending_);
    r.read_array(s.delay_);
    if (!r.ok() || tag != 0x4C01U || s.head_ >= 128U || s.count_ > 128U || s.phase_ >= cpu_clock_ ||
        s.reset_clocks_ > 16000U || s.divider_ >= 80U || s.frame_sample_ >= 200U ||
        s.field_ > 12U || repeat > 1U || playing > 1U || interpolate > 1U ||
        s.pitch_count_ >= 159U || s.noise_ == 0U || s.noise_ > 0x1FFFU || (s.output_ & 255) != 0 ||
        !valid_frame(s.current_) || !valid_frame(s.target_) || !valid_frame(s.pending_) ||
        !std::ranges::all_of(s.delay_, [](int16_t x) { return x >= -16384 && x <= 16383; }) ||
        (playing != 0U && s.field_ != 0U) ||
        (s.reset_clocks_ != 0U && (playing != 0U || s.field_ != 0U || s.divider_ != 0U))) {
        r.fail();
        return;
    }
    s.repeat_ = repeat != 0U;
    s.playing_ = playing != 0U;
    s.interpolate_ = interpolate != 0U;
    *this = s;
}
} // namespace mapperbus::core
