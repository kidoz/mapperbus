#include "core/apu/apu.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mapperbus::core {

// --- Envelope ---

void Envelope::clock() {
    if (start) {
        start = false;
        decay_level = 15;
        divider = period;
    } else {
        if (divider == 0) {
            divider = period;
            if (decay_level > 0) {
                --decay_level;
            } else if (loop) {
                decay_level = 15;
            }
        } else {
            --divider;
        }
    }
}

uint8_t Envelope::volume() const {
    return constant_volume ? period : decay_level;
}

// --- Sweep ---

uint16_t Sweep::target_period(uint16_t current) const {
    uint16_t change = current >> shift;
    if (negate) {
        if (is_pulse1) {
            return current - change - 1;
        }
        return current - change;
    }
    return current + change;
}

bool Sweep::muting(uint16_t timer_period) const {
    return timer_period < 8 || target_period(timer_period) > 0x7FF;
}

void Sweep::clock(uint16_t& timer_period) {
    if (divider == 0 && enabled && !muting(timer_period) && shift > 0) {
        timer_period = target_period(timer_period);
    }
    if (divider == 0 || reload) {
        divider = period;
        reload = false;
    } else {
        --divider;
    }
}

// --- Pulse Channel ---

void PulseChannel::clock_timer() {
    if (timer == 0) {
        timer = timer_period;
        sequence_pos = (sequence_pos + 1) & 0x07;
    } else {
        --timer;
    }
}

uint8_t PulseChannel::output() const {
    if (!enabled)
        return 0;
    if (length_counter == 0)
        return 0;
    if (kDutyTable[duty][sequence_pos] == 0)
        return 0;
    if (sweep.muting(timer_period))
        return 0;
    return envelope.volume();
}

// --- Triangle Channel ---

void TriangleChannel::clock_timer() {
    if (timer == 0) {
        timer = timer_period;
        if (length_counter > 0 && linear_counter > 0) {
            sequence_pos = (sequence_pos + 1) & 0x1F;
        }
    } else {
        --timer;
    }
}

void TriangleChannel::clock_linear_counter() {
    if (linear_counter_reload_flag) {
        linear_counter = linear_counter_reload;
    } else if (linear_counter > 0) {
        --linear_counter;
    }
    if (!control_flag) {
        linear_counter_reload_flag = false;
    }
}

uint8_t TriangleChannel::output() const {
    if (!enabled)
        return 0;
    if (length_counter == 0)
        return 0;
    if (linear_counter == 0)
        return 0;
    if (timer_period < 2)
        return 7;
    return kTriangleTable[sequence_pos];
}

// --- Noise Channel ---

void NoiseChannel::clock_timer() {
    if (timer == 0) {
        timer = timer_period;
        uint16_t feedback_bit = mode ? 6 : 1;
        uint16_t feedback = (shift_register & 0x01) ^ ((shift_register >> feedback_bit) & 0x01);
        shift_register >>= 1;
        shift_register |= (feedback << 14);
    } else {
        --timer;
    }
}

uint8_t NoiseChannel::output() const {
    if (!enabled)
        return 0;
    if (length_counter == 0)
        return 0;
    if (shift_register & 0x01)
        return 0;
    return envelope.volume();
}

// --- DMC Channel ---

void DmcChannel::clock_timer() {
    if (bits_remaining == 0) {
        if (!sample_buffer_empty) {
            shift_register = sample_buffer;
            sample_buffer_empty = true;
            bits_remaining = 8;
        }
        return;
    }

    if (shift_register & 0x01) {
        if (output_level <= 125)
            output_level += 2;
    } else {
        if (output_level >= 2)
            output_level -= 2;
    }
    shift_register >>= 1;
    --bits_remaining;
}

// --- AudioFilter (first-order IIR) ---

float AudioFilter::apply(float input) {
    if (is_highpass) {
        // y[n] = alpha * (y[n-1] + x[n] - x[n-1])
        float output = alpha * (prev_output + input - prev_input);
        prev_input = input;
        prev_output = output;
        return output;
    }
    // Low-pass: y[n] = alpha * x[n] + (1 - alpha) * y[n-1]
    float output = alpha * input + (1.0f - alpha) * prev_output;
    prev_output = output;
    prev_input = input;
    return output;
}

// --- BiquadFilter (second-order IIR, Direct Form II Transposed) ---

float BiquadFilter::apply(float input) {
    float output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    return output;
}

BiquadFilter BiquadFilter::butterworth_lowpass(float cutoff_hz, float sample_rate) {
    float omega = 2.0f * std::numbers::pi_v<float> * cutoff_hz / sample_rate;
    float sn = std::sin(omega);
    float cs = std::cos(omega);
    // Q = 1/sqrt(2) for Butterworth
    float alpha = sn / (2.0f * std::numbers::sqrt2_v<float>);

    float a0 = 1.0f + alpha;

    BiquadFilter f;
    f.b0 = ((1.0f - cs) / 2.0f) / a0;
    f.b1 = (1.0f - cs) / a0;
    f.b2 = f.b0;
    f.a1 = (-2.0f * cs) / a0;
    f.a2 = (1.0f - alpha) / a0;
    return f;
}

BiquadFilter BiquadFilter::butterworth_highpass(float cutoff_hz, float sample_rate) {
    float omega = 2.0f * std::numbers::pi_v<float> * cutoff_hz / sample_rate;
    float sn = std::sin(omega);
    float cs = std::cos(omega);
    float alpha = sn / (2.0f * std::numbers::sqrt2_v<float>);

    float a0 = 1.0f + alpha;

    BiquadFilter f;
    f.b0 = ((1.0f + cs) / 2.0f) / a0;
    f.b1 = (-(1.0f + cs)) / a0;
    f.b2 = f.b0;
    f.a1 = (-2.0f * cs) / a0;
    f.a2 = (1.0f - alpha) / a0;
    return f;
}

// --- MixerTables ---

void MixerTables::compute() {
    pulse[0] = 0.0f;
    for (int i = 1; i <= 30; ++i) {
        pulse[i] = 95.52f / (8128.0f / static_cast<float>(i) + 100.0f);
    }
    tnd[0] = 0.0f;
    for (int i = 1; i <= 202; ++i) {
        tnd[i] = 163.67f / (24329.0f / static_cast<float>(i) + 100.0f);
    }
}

// --- APU ---

Apu::Apu() : Apu(AudioSettings{}) {}

Apu::Apu(const AudioSettings& settings) : settings_(settings) {
    pulse1_.sweep.is_pulse1 = true;
    pulse2_.sweep.is_pulse1 = false;

    sample_rate_ = settings_.sample_rate;
    base_cycles_per_sample_ = cpu_clock_ / static_cast<double>(sample_rate_);
    cycles_per_sample_ = base_cycles_per_sample_;

    mixer_.compute();
    init_filters();

    // Configure BlipBuffer
    blip_buffer_.set_rates(cpu_clock_, static_cast<double>(sample_rate_));
}

void Apu::init_filters() {
    auto fs = static_cast<float>(sample_rate_);

    // First-order filters (HardwareAccurate mode)
    lp_filter_.alpha = AudioFilter::lp_alpha(14000.0f, fs);
    lp_filter_.is_highpass = false;

    if (settings_.filter_profile == FilterProfile::NES) {
        hp_filter1_.alpha = AudioFilter::hp_alpha(37.0f, fs);
        hp_filter2_.alpha = AudioFilter::hp_alpha(667.0f, fs);
    } else {
        // Famicom profile
        hp_filter1_.alpha = AudioFilter::hp_alpha(90.0f, fs);
        hp_filter2_.alpha = AudioFilter::hp_alpha(440.0f, fs);
    }
    hp_filter1_.is_highpass = true;
    hp_filter2_.is_highpass = true;

    // Biquad filters (Enhanced mode)
    biquad_lp_ = BiquadFilter::butterworth_lowpass(14000.0f, fs);

    if (settings_.filter_profile == FilterProfile::NES) {
        biquad_hp1_ = BiquadFilter::butterworth_highpass(37.0f, fs);
        biquad_hp2_ = BiquadFilter::butterworth_highpass(667.0f, fs);
    } else {
        biquad_hp1_ = BiquadFilter::butterworth_highpass(90.0f, fs);
        biquad_hp2_ = BiquadFilter::butterworth_highpass(440.0f, fs);
    }
}

void Apu::reset() {
    pulse1_ = {};
    pulse1_.sweep.is_pulse1 = true;
    pulse2_ = {};
    pulse2_.sweep.is_pulse1 = false;
    triangle_ = {};
    noise_ = {};
    noise_.shift_register = 1;
    dmc_ = {};
    frame_counter_cycles_ = 0;
    frame_counter_mode_ = false;
    frame_irq_inhibit_ = false;
    frame_irq_pending_ = false;
    cycle_accumulator_ = 0.0;
    current_mix_ = 0.0f;
    sample_history_.fill(0.0f);
    lp_filter_.reset();
    hp_filter1_.reset();
    hp_filter2_.reset();
    biquad_lp_.reset();
    biquad_hp1_.reset();
    biquad_hp2_.reset();
    drc_rate_ = 1.0;
    dmc_stall_cycles_ = 0;
    output_ring_.reset();
    staging_buffer_.clear();
    prev_blip_mix_ = 0.0f;
    blip_cycle_offset_ = 0;
    blip_buffer_.reset();
    dither_state_ = 1;
    prev_dither_random_ = 0.0f;
}

void Apu::set_region(Region region) {
    switch (region) {
    case Region::PAL:
        noise_period_table_ = &kNoisePeriodPal;
        dmc_rate_table_ = &kDmcRatePal;
        frame_timing_ = &kFrameCounterPal;
        cpu_clock_ = kCpuClockPal;
        break;
    case Region::Dendy:
        noise_period_table_ = &kNoisePeriodNtsc;
        dmc_rate_table_ = &kDmcRateNtsc;
        frame_timing_ = &kFrameCounterNtsc;
        cpu_clock_ = kCpuClockDendy;
        break;
    default:
        noise_period_table_ = &kNoisePeriodNtsc;
        dmc_rate_table_ = &kDmcRateNtsc;
        frame_timing_ = &kFrameCounterNtsc;
        cpu_clock_ = kCpuClockNtsc;
        break;
    }
    base_cycles_per_sample_ = cpu_clock_ / static_cast<double>(sample_rate_);
    cycles_per_sample_ = base_cycles_per_sample_ / drc_rate_;
    blip_buffer_.set_rates(cpu_clock_, static_cast<double>(sample_rate_));
}

void Apu::set_sample_rate(int sample_rate) {
    sample_rate_ = sample_rate;
    base_cycles_per_sample_ = cpu_clock_ / static_cast<double>(sample_rate_);
    cycles_per_sample_ = base_cycles_per_sample_ / drc_rate_;
    init_filters();
    blip_buffer_.set_rates(cpu_clock_, static_cast<double>(sample_rate_));
}

void Apu::set_memory_reader(MemoryReader reader) {
    memory_reader_ = std::move(reader);
}

void Apu::set_expansion_audio(ExpansionAudioSource source) {
    expansion_audio_ = std::move(source);
}

uint32_t Apu::take_dmc_stall_cycles() {
    uint32_t cycles = dmc_stall_cycles_;
    dmc_stall_cycles_ = 0;
    return cycles;
}

void Apu::update_rate_control(float buffer_fill_ratio) {
    // Target 50% buffer fill. Adjust rate by up to +/-0.5%.
    constexpr float kTarget = 0.5f;
    constexpr float kDeadzone = 0.05f;
    constexpr double kDelta = 0.005;

    float error = buffer_fill_ratio - kTarget;
    if (std::abs(error) > kDeadzone) {
        if (error > 0.0f) {
            drc_rate_ = std::max(drc_rate_ - kDelta, 0.995);
        } else {
            drc_rate_ = std::min(drc_rate_ + kDelta, 1.005);
        }
    } else {
        // Slowly return to 1.0
        drc_rate_ += (1.0 - drc_rate_) * 0.01;
    }
    cycles_per_sample_ = base_cycles_per_sample_ / drc_rate_;

    // Adjust BlipBuffer rate to match DRC-corrected sample rate
    if (settings_.resampling == ResamplingMode::BlipBuffer) {
        double adjusted_rate = static_cast<double>(sample_rate_) * drc_rate_;
        blip_buffer_.set_rates(cpu_clock_, adjusted_rate);
    }
}

void Apu::step(uint32_t cpu_cycles) {
    bool use_blip = settings_.resampling == ResamplingMode::BlipBuffer;

    for (uint32_t i = 0; i < cpu_cycles; ++i) {
        pulse1_.clock_timer();
        pulse2_.clock_timer();
        triangle_.clock_timer();
        noise_.clock_timer();

        if (dmc_.enabled) {
            if (dmc_.timer == 0) {
                dmc_.timer = dmc_.timer_period;
                clock_dmc();
            } else {
                --dmc_.timer;
            }

            if (dmc_.sample_buffer_empty && dmc_.bytes_remaining > 0 && memory_reader_) {
                dmc_.sample_buffer = memory_reader_(dmc_.current_address);
                dmc_.sample_buffer_empty = false;
                dmc_stall_cycles_ += 4; // DMC read stalls CPU for 4 cycles
                dmc_.current_address = static_cast<Address>(
                    (dmc_.current_address == 0xFFFF) ? 0x8000 : dmc_.current_address + 1);
                --dmc_.bytes_remaining;

                if (dmc_.bytes_remaining == 0) {
                    if (dmc_.loop_flag) {
                        dmc_.current_address = dmc_.sample_address;
                        dmc_.bytes_remaining = dmc_.sample_length;
                    } else if (dmc_.irq_enabled) {
                        dmc_.irq_pending = true;
                    }
                }
            }
        }

        ++frame_counter_cycles_;
        clock_frame_counter();

        // Compute current mix
        current_mix_ = mix();

        if (use_blip) {
            // BlipBuffer mode: record amplitude deltas at cycle-precise timestamps
            float delta = current_mix_ - prev_blip_mix_;
            if (std::abs(delta) > 1e-8f) {
                blip_buffer_.add_delta(blip_cycle_offset_, delta);
                prev_blip_mix_ = current_mix_;
            }
            ++blip_cycle_offset_;
        } else {
            // Cubic Hermite mode
            cycle_accumulator_ += 1.0;
            if (cycle_accumulator_ >= cycles_per_sample_) {
                cycle_accumulator_ -= cycles_per_sample_;
                emit_sample();
            }
        }
    }
}

void Apu::end_audio_frame() {
    if (settings_.resampling != ResamplingMode::BlipBuffer)
        return;
    if (blip_cycle_offset_ == 0)
        return;

    blip_buffer_.end_frame(blip_cycle_offset_);
    blip_cycle_offset_ = 0;

    // Read all available samples from BlipBuffer, filter, and push to ring
    int avail = blip_buffer_.samples_available();
    if (avail <= 0)
        return;

    // Use stack buffer to avoid per-frame heap allocation.
    // ~800 samples/frame at 48kHz NTSC; 2048 covers all practical cases.
    std::array<float, 2048> temp{};
    int to_read = std::min(avail, static_cast<int>(temp.size()));
    int count = blip_buffer_.read_samples(temp.data(), to_read);

    bool stereo = settings_.stereo_mode == StereoMode::PseudoStereo;
    for (int i = 0; i < count; ++i) {
        float sample = filter(temp[static_cast<size_t>(i)]);
        if (settings_.dithering_enabled) {
            sample += tpdf_dither();
        }
        if (stereo) {
            // BlipBuffer operates on the mixed signal. Derive stereo by
            // computing per-channel panned mix from current channel outputs.
            auto st = mix_stereo();
            float left = filter(st.left);
            float right = filter(st.right);
            if (settings_.dithering_enabled) {
                left += tpdf_dither();
                right += tpdf_dither();
            }
            output_ring_.try_push(left);
            output_ring_.try_push(right);
        } else {
            output_ring_.try_push(sample);
        }
    }
}

void Apu::emit_sample() {
    // Shift history for cubic interpolation
    sample_history_[0] = sample_history_[1];
    sample_history_[1] = sample_history_[2];
    sample_history_[2] = sample_history_[3];
    sample_history_[3] = current_mix_;

    // Cubic Hermite interpolation (Catmull-Rom) using fractional position
    float t = static_cast<float>(cycle_accumulator_ / cycles_per_sample_);
    float p0 = sample_history_[0];
    float p1 = sample_history_[1];
    float p2 = sample_history_[2];
    float p3 = sample_history_[3];

    float t2 = t * t;
    float t3 = t2 * t;
    float sample =
        0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);

    if (settings_.stereo_mode == StereoMode::PseudoStereo) {
        auto stereo = mix_stereo();
        float left = filter(stereo.left);
        float right = filter(stereo.right);
        if (settings_.dithering_enabled) {
            left += tpdf_dither();
            right += tpdf_dither();
        }
        output_ring_.try_push(left);
        output_ring_.try_push(right);
    } else {
        sample = filter(sample);
        if (settings_.dithering_enabled) {
            sample += tpdf_dither();
        }
        output_ring_.try_push(sample);
    }
}

void Apu::clock_dmc() {
    dmc_.clock_timer();
}

void Apu::clock_frame_counter() {
    if (!frame_counter_mode_) {
        if (frame_counter_cycles_ == frame_timing_->step1) {
            clock_quarter_frame();
        } else if (frame_counter_cycles_ == frame_timing_->step2) {
            clock_quarter_frame();
            clock_half_frame();
        } else if (frame_counter_cycles_ == frame_timing_->step3) {
            clock_quarter_frame();
        } else if (frame_counter_cycles_ == frame_timing_->step4) {
            clock_quarter_frame();
            clock_half_frame();
            if (!frame_irq_inhibit_) {
                frame_irq_pending_ = true;
            }
            frame_counter_cycles_ = 0;
        }
    } else {
        if (frame_counter_cycles_ == frame_timing_->step1) {
            clock_quarter_frame();
        } else if (frame_counter_cycles_ == frame_timing_->step2) {
            clock_quarter_frame();
            clock_half_frame();
        } else if (frame_counter_cycles_ == frame_timing_->step3) {
            clock_quarter_frame();
        } else if (frame_counter_cycles_ == frame_timing_->step5) {
            clock_quarter_frame();
            clock_half_frame();
            frame_counter_cycles_ = 0;
        }
    }
}

void Apu::clock_quarter_frame() {
    pulse1_.envelope.clock();
    pulse2_.envelope.clock();
    triangle_.clock_linear_counter();
    noise_.envelope.clock();
}

void Apu::clock_half_frame() {
    if (pulse1_.length_counter > 0 && !pulse1_.halt_length)
        --pulse1_.length_counter;
    if (pulse2_.length_counter > 0 && !pulse2_.halt_length)
        --pulse2_.length_counter;
    if (triangle_.length_counter > 0 && !triangle_.control_flag)
        --triangle_.length_counter;
    if (noise_.length_counter > 0 && !noise_.halt_length)
        --noise_.length_counter;

    pulse1_.sweep.clock(pulse1_.timer_period);
    pulse2_.sweep.clock(pulse2_.timer_period);
}

float Apu::mix() const {
    // Lookup-table based non-linear mixing (NESdev wiki)
    uint8_t pulse_idx = static_cast<uint8_t>(std::min(30, pulse1_.output() + pulse2_.output()));
    // TND index: 3*triangle + 2*noise + dmc (max = 3*15 + 2*15 + 127 = 202)
    int tnd_idx = std::min(202, 3 * triangle_.output() + 2 * noise_.output() + dmc_.output());

    float internal = mixer_.pulse[pulse_idx] + mixer_.tnd[tnd_idx];

    // Add expansion audio
    if (expansion_audio_) {
        float exp = expansion_audio_();
        if (settings_.expansion_mixing == ExpansionMixingMode::ResistanceModeled) {
            // Famicom resistor divider: expansion port ~47kOhm, internal ~15kOhm
            // Ratio k = 15/(15+47) ~= 0.242
            constexpr float kExpansionRatio = 0.242f;
            return internal * (1.0f - kExpansionRatio) + exp * kExpansionRatio;
        }
        return internal + exp;
    }

    return internal;
}

StereoSample Apu::mix_stereo() const {
    // Per-channel linearized outputs for stereo panning
    float p1 = mixer_.pulse[std::min(15, static_cast<int>(pulse1_.output()))];
    float p2 = mixer_.pulse[std::min(15, static_cast<int>(pulse2_.output()))];
    float tri_val = static_cast<float>(triangle_.output()) / 15.0f * 0.15f;
    float noi_val = static_cast<float>(noise_.output()) / 15.0f * 0.12f;
    float dmc_val = static_cast<float>(dmc_.output()) / 127.0f * 0.20f;

    float exp_val = 0.0f;
    if (expansion_audio_) {
        exp_val = expansion_audio_();
        if (settings_.expansion_mixing == ExpansionMixingMode::ResistanceModeled) {
            exp_val *= 0.242f;
        }
    }

    float left = p1 * kPanPulse1.left + p2 * kPanPulse2.left + tri_val * kPanTriangle.left +
                 noi_val * kPanNoise.left + dmc_val * kPanDmc.left + exp_val * kPanExpansion.left;

    float right = p1 * kPanPulse1.right + p2 * kPanPulse2.right + tri_val * kPanTriangle.right +
                  noi_val * kPanNoise.right + dmc_val * kPanDmc.right +
                  exp_val * kPanExpansion.right;

    return {left, right};
}

float Apu::filter(float sample) {
    if (settings_.filter_mode == FilterMode::Unfiltered) {
        return sample;
    }

    if (settings_.filter_mode == FilterMode::Enhanced) {
        // Second-order Butterworth filter chain
        sample = biquad_lp_.apply(sample);
        sample = biquad_hp1_.apply(sample);
        sample = biquad_hp2_.apply(sample);
    } else {
        // First-order IIR filter chain (NES hardware accurate)
        sample = lp_filter_.apply(sample);
        sample = hp_filter1_.apply(sample);
        sample = hp_filter2_.apply(sample);
    }
    return sample;
}

float Apu::tpdf_dither() {
    // Fast xorshift32 PRNG
    auto xorshift = [](uint32_t& state) -> float {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state) / static_cast<float>(UINT32_MAX) - 0.5f;
    };
    float r1 = xorshift(dither_state_);
    
    // High-Pass TPDF dither (shapes noise to very high frequencies)
    float dither = r1 - prev_dither_random_;
    prev_dither_random_ = r1;

    // Scale to 1 LSB of 16-bit audio
    constexpr float kDitherScale = 1.0f / 32768.0f;
    return dither * kDitherScale;
}

size_t Apu::drain_samples(float* dest, size_t max_count) {
    return output_ring_.read(dest, max_count);
}

std::span<const float> Apu::output_buffer() const {
    // Legacy compat: drain ring into staging vector
    staging_buffer_.clear();
    size_t avail = output_ring_.available();
    if (avail > 0) {
        staging_buffer_.resize(avail);
        size_t n = const_cast<RingBuffer<float>&>(output_ring_).read(staging_buffer_.data(), avail);
        staging_buffer_.resize(n);
    }
    return staging_buffer_;
}

void Apu::clear_output_buffer() {
    // Ring buffer is self-clearing on read; staging buffer cleared on next output_buffer() call
    staging_buffer_.clear();
}

Byte Apu::read_register(Address addr) {
    if (addr == 0x4015) {
        Byte status = 0;
        if (pulse1_.length_counter > 0)
            status |= 0x01;
        if (pulse2_.length_counter > 0)
            status |= 0x02;
        if (triangle_.length_counter > 0)
            status |= 0x04;
        if (noise_.length_counter > 0)
            status |= 0x08;
        if (dmc_.bytes_remaining > 0)
            status |= 0x10;
        if (frame_irq_pending_)
            status |= 0x40;
        if (dmc_.irq_pending)
            status |= 0x80;
        frame_irq_pending_ = false;
        return status;
    }
    return 0;
}

void Apu::write_register(Address addr, Byte value) {
    switch (addr) {
    case 0x4000:
        pulse1_.duty = (value >> 6) & 0x03;
        pulse1_.halt_length = (value & 0x20) != 0;
        pulse1_.envelope.loop = (value & 0x20) != 0;
        pulse1_.envelope.constant_volume = (value & 0x10) != 0;
        pulse1_.envelope.period = value & 0x0F;
        break;
    case 0x4001:
        pulse1_.sweep.enabled = (value & 0x80) != 0;
        pulse1_.sweep.period = (value >> 4) & 0x07;
        pulse1_.sweep.negate = (value & 0x08) != 0;
        pulse1_.sweep.shift = value & 0x07;
        pulse1_.sweep.reload = true;
        break;
    case 0x4002:
        pulse1_.timer_period = (pulse1_.timer_period & 0x0700) | value;
        break;
    case 0x4003:
        pulse1_.timer_period =
            (pulse1_.timer_period & 0x00FF) | (static_cast<uint16_t>(value & 0x07) << 8);
        if (pulse1_.enabled)
            pulse1_.length_counter = kLengthTable[(value >> 3) & 0x1F];
        pulse1_.sequence_pos = 0;
        pulse1_.envelope.start = true;
        break;
    case 0x4004:
        pulse2_.duty = (value >> 6) & 0x03;
        pulse2_.halt_length = (value & 0x20) != 0;
        pulse2_.envelope.loop = (value & 0x20) != 0;
        pulse2_.envelope.constant_volume = (value & 0x10) != 0;
        pulse2_.envelope.period = value & 0x0F;
        break;
    case 0x4005:
        pulse2_.sweep.enabled = (value & 0x80) != 0;
        pulse2_.sweep.period = (value >> 4) & 0x07;
        pulse2_.sweep.negate = (value & 0x08) != 0;
        pulse2_.sweep.shift = value & 0x07;
        pulse2_.sweep.reload = true;
        break;
    case 0x4006:
        pulse2_.timer_period = (pulse2_.timer_period & 0x0700) | value;
        break;
    case 0x4007:
        pulse2_.timer_period =
            (pulse2_.timer_period & 0x00FF) | (static_cast<uint16_t>(value & 0x07) << 8);
        if (pulse2_.enabled)
            pulse2_.length_counter = kLengthTable[(value >> 3) & 0x1F];
        pulse2_.sequence_pos = 0;
        pulse2_.envelope.start = true;
        break;
    case 0x4008:
        triangle_.control_flag = (value & 0x80) != 0;
        triangle_.linear_counter_reload = value & 0x7F;
        break;
    case 0x400A:
        triangle_.timer_period = (triangle_.timer_period & 0x0700) | value;
        break;
    case 0x400B:
        triangle_.timer_period =
            (triangle_.timer_period & 0x00FF) | (static_cast<uint16_t>(value & 0x07) << 8);
        if (triangle_.enabled)
            triangle_.length_counter = kLengthTable[(value >> 3) & 0x1F];
        triangle_.linear_counter_reload_flag = true;
        break;
    case 0x400C:
        noise_.halt_length = (value & 0x20) != 0;
        noise_.envelope.loop = (value & 0x20) != 0;
        noise_.envelope.constant_volume = (value & 0x10) != 0;
        noise_.envelope.period = value & 0x0F;
        break;
    case 0x400E:
        noise_.mode = (value & 0x80) != 0;
        noise_.timer_period = (*noise_period_table_)[value & 0x0F];
        break;
    case 0x400F:
        if (noise_.enabled)
            noise_.length_counter = kLengthTable[(value >> 3) & 0x1F];
        noise_.envelope.start = true;
        break;
    case 0x4010:
        dmc_.irq_enabled = (value & 0x80) != 0;
        dmc_.loop_flag = (value & 0x40) != 0;
        dmc_.timer_period = (*dmc_rate_table_)[value & 0x0F];
        if (!dmc_.irq_enabled)
            dmc_.irq_pending = false;
        break;
    case 0x4011:
        dmc_.output_level = value & 0x7F;
        break;
    case 0x4012:
        dmc_.sample_address = static_cast<Address>(0xC000 | (static_cast<uint16_t>(value) << 6));
        break;
    case 0x4013:
        dmc_.sample_length = static_cast<uint16_t>(value << 4) + 1;
        break;
    case 0x4015:
        pulse1_.enabled = (value & 0x01) != 0;
        pulse2_.enabled = (value & 0x02) != 0;
        triangle_.enabled = (value & 0x04) != 0;
        noise_.enabled = (value & 0x08) != 0;
        dmc_.enabled = (value & 0x10) != 0;
        if (!pulse1_.enabled)
            pulse1_.length_counter = 0;
        if (!pulse2_.enabled)
            pulse2_.length_counter = 0;
        if (!triangle_.enabled)
            triangle_.length_counter = 0;
        if (!noise_.enabled)
            noise_.length_counter = 0;
        dmc_.irq_pending = false;
        if (!dmc_.enabled) {
            dmc_.bytes_remaining = 0;
        } else if (dmc_.bytes_remaining == 0) {
            dmc_.current_address = dmc_.sample_address;
            dmc_.bytes_remaining = dmc_.sample_length;
        }
        break;
    case 0x4017:
        frame_counter_mode_ = (value & 0x80) != 0;
        frame_irq_inhibit_ = (value & 0x40) != 0;
        if (frame_irq_inhibit_)
            frame_irq_pending_ = false;
        frame_counter_cycles_ = 0;
        if (frame_counter_mode_) {
            clock_quarter_frame();
            clock_half_frame();
        }
        break;
    default:
        break;
    }
}

} // namespace mapperbus::core
