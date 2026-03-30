#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numbers>
#include <span>
#include <vector>

#include "core/apu/audio_settings.hpp"
#include "core/apu/blip_buffer.hpp"
#include "core/types.hpp"

namespace mapperbus::core {

// Length counter lookup table
inline constexpr std::array<uint8_t, 32> kLengthTable = {
    10, 254, 20, 2,  40, 4,  80, 6,  160, 8,  60, 10, 14, 12, 26, 14,
    12, 16,  24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30,
};

// clang-format off
inline constexpr std::array<uint16_t, 16> kNoisePeriodNtsc = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068,
};
inline constexpr std::array<uint16_t, 16> kNoisePeriodPal = {
    4, 8, 14, 30, 60, 88, 118, 148, 188, 236, 354, 472, 708,  944, 1890, 3778,
};
inline constexpr std::array<uint16_t, 16> kDmcRateNtsc = {
    428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54,
};
inline constexpr std::array<uint16_t, 16> kDmcRatePal = {
    398, 354, 316, 298, 276, 236, 210, 198, 176, 148, 132, 118,  98, 78, 66, 50,
};
// clang-format on

inline constexpr double kCpuClockNtsc = 1789773.0;
inline constexpr double kCpuClockPal = 1662607.0;
inline constexpr double kCpuClockDendy = 1773448.0;

struct FrameCounterTiming {
    uint32_t step1;
    uint32_t step2;
    uint32_t step3;
    uint32_t step4;
    uint32_t step5;
};

inline constexpr FrameCounterTiming kFrameCounterNtsc = {7457, 14913, 22371, 29829, 37281};
inline constexpr FrameCounterTiming kFrameCounterPal = {8313, 16627, 24939, 33253, 41565};

inline constexpr std::array<std::array<uint8_t, 8>, 4> kDutyTable = {{
    {0, 1, 0, 0, 0, 0, 0, 0},
    {0, 1, 1, 0, 0, 0, 0, 0},
    {0, 1, 1, 1, 1, 0, 0, 0},
    {1, 0, 0, 1, 1, 1, 1, 1},
}};

inline constexpr std::array<uint8_t, 32> kTriangleTable = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5,  4,  3,  2,  1,  0,
    0,  1,  2,  3,  4,  5,  6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};

struct Envelope {
    bool start = false;
    bool loop = false;
    bool constant_volume = false;
    uint8_t period = 0;
    uint8_t divider = 0;
    uint8_t decay_level = 0;

    void clock();
    uint8_t volume() const;
};

struct Sweep {
    bool enabled = false;
    bool negate = false;
    bool reload = false;
    uint8_t period = 0;
    uint8_t shift = 0;
    uint8_t divider = 0;
    bool is_pulse1 = false;

    void clock(uint16_t& timer_period);
    uint16_t target_period(uint16_t current) const;
    bool muting(uint16_t timer_period) const;
};

struct PulseChannel {
    uint8_t duty = 0;
    uint8_t sequence_pos = 0;
    uint16_t timer_period = 0;
    uint16_t timer = 0;
    uint8_t length_counter = 0;
    bool enabled = false;
    bool halt_length = false;
    Envelope envelope;
    Sweep sweep;

    void clock_timer();
    uint8_t output() const;
};

struct TriangleChannel {
    uint16_t timer_period = 0;
    uint16_t timer = 0;
    uint8_t sequence_pos = 0;
    uint8_t length_counter = 0;
    uint8_t linear_counter = 0;
    uint8_t linear_counter_reload = 0;
    bool linear_counter_reload_flag = false;
    bool control_flag = false;
    bool enabled = false;

    void clock_timer();
    void clock_linear_counter();
    uint8_t output() const;
};

struct NoiseChannel {
    uint16_t timer_period = 0;
    uint16_t timer = 0;
    uint16_t shift_register = 1;
    bool mode = false;
    uint8_t length_counter = 0;
    bool enabled = false;
    bool halt_length = false;
    Envelope envelope;

    void clock_timer();
    uint8_t output() const;
};

struct DmcChannel {
    bool enabled = false;
    bool irq_enabled = false;
    bool loop_flag = false;
    bool irq_pending = false;

    uint16_t timer = 0;
    uint16_t timer_period = 0;
    uint8_t output_level = 0;

    Address sample_address = 0xC000;
    uint16_t sample_length = 1;
    Address current_address = 0xC000;
    uint16_t bytes_remaining = 0;

    Byte sample_buffer = 0;
    bool sample_buffer_empty = true;
    Byte shift_register = 0;
    uint8_t bits_remaining = 0;

    void clock_timer();
    uint8_t output() const {
        return output_level;
    }
};

// First-order IIR filter (high-pass or low-pass)
struct AudioFilter {
    float prev_input = 0.0f;
    float prev_output = 0.0f;
    float alpha = 0.0f;
    bool is_highpass = true;

    float apply(float input);
    void reset() {
        prev_input = 0.0f;
        prev_output = 0.0f;
    }

    static float hp_alpha(float cutoff_hz, float sample_rate) {
        float rc = 1.0f / (2.0f * std::numbers::pi_v<float> * cutoff_hz);
        float dt = 1.0f / sample_rate;
        return rc / (rc + dt);
    }

    static float lp_alpha(float cutoff_hz, float sample_rate) {
        float rc = 1.0f / (2.0f * std::numbers::pi_v<float> * cutoff_hz);
        float dt = 1.0f / sample_rate;
        return dt / (rc + dt);
    }
};

// Second-order IIR filter (biquad, Direct Form II Transposed)
struct BiquadFilter {
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    float apply(float input);
    void reset() {
        z1 = 0.0f;
        z2 = 0.0f;
    }

    static BiquadFilter butterworth_lowpass(float cutoff_hz, float sample_rate);
    static BiquadFilter butterworth_highpass(float cutoff_hz, float sample_rate);
};

// Pre-computed mixer lookup tables (NESdev wiki formulas)
struct MixerTables {
    std::array<float, 31> pulse{}; // pulse1 + pulse2 (0-30)
    std::array<float, 203> tnd{};  // 3*tri + 2*noise + dmc (0-202)

    void compute();
};

// Per-channel stereo panning weights
struct ChannelPan {
    float left;
    float right;
};

inline constexpr ChannelPan kPanPulse1 = {0.75f, 0.25f};
inline constexpr ChannelPan kPanPulse2 = {0.25f, 0.75f};
inline constexpr ChannelPan kPanTriangle = {0.5f, 0.5f};
inline constexpr ChannelPan kPanNoise = {0.6f, 0.4f};
inline constexpr ChannelPan kPanDmc = {0.5f, 0.5f};
inline constexpr ChannelPan kPanExpansion = {0.5f, 0.5f};

struct StereoSample {
    float left;
    float right;
};

// SPSC lock-free ring buffer for audio output
template <typename T> class RingBuffer {
  public:
    explicit RingBuffer(size_t capacity) {
        // Round up to next power of 2
        size_t n = 1;
        while (n < capacity)
            n <<= 1;
        buffer_.resize(n);
        mask_ = n - 1;
    }

    bool try_push(T value) {
        size_t w = write_pos_.load(std::memory_order_relaxed);
        size_t r = read_pos_.load(std::memory_order_acquire);
        if (w - r >= buffer_.size())
            return false;
        buffer_[w & mask_] = value;
        write_pos_.store(w + 1, std::memory_order_release);
        return true;
    }

    size_t read(T* dest, size_t max_count) {
        size_t r = read_pos_.load(std::memory_order_relaxed);
        size_t w = write_pos_.load(std::memory_order_acquire);
        size_t avail = w - r;
        size_t count = std::min(avail, max_count);
        for (size_t i = 0; i < count; ++i) {
            dest[i] = buffer_[(r + i) & mask_];
        }
        read_pos_.store(r + count, std::memory_order_release);
        return count;
    }

    size_t available() const {
        size_t w = write_pos_.load(std::memory_order_acquire);
        size_t r = read_pos_.load(std::memory_order_acquire);
        return w - r;
    }

    void reset() {
        write_pos_.store(0, std::memory_order_relaxed);
        read_pos_.store(0, std::memory_order_relaxed);
    }

  private:
    std::vector<T> buffer_;
    size_t mask_ = 0;
    std::atomic<size_t> write_pos_{0};
    std::atomic<size_t> read_pos_{0};
};

using MemoryReader = std::function<Byte(Address)>;
using ExpansionAudioSource = std::function<float()>;

class Apu {
  public:
    Apu();
    explicit Apu(const AudioSettings& settings);

    void reset();
    void step(uint32_t cpu_cycles);

    Byte read_register(Address addr);
    void write_register(Address addr, Byte value);

    /// Legacy interface: returns a span over an internal staging buffer.
    /// Prefer drain_samples() for new code.
    std::span<const float> output_buffer() const;
    void clear_output_buffer();

    /// Read available samples from the ring buffer into dest.
    /// Returns number of samples read.
    size_t drain_samples(float* dest, size_t max_count);

    void set_region(Region region);
    void set_sample_rate(int sample_rate);
    void set_memory_reader(MemoryReader reader);
    void set_expansion_audio(ExpansionAudioSource source);

    const AudioSettings& settings() const {
        return settings_;
    }

    bool irq_pending() const {
        return frame_irq_pending_ || dmc_.irq_pending;
    }

    /// Returns and clears accumulated DMC memory read stall cycles.
    uint32_t take_dmc_stall_cycles();

    /// Call from frontend with current audio buffer fill ratio (0.0-1.0)
    /// to enable dynamic rate control for audio-video sync.
    void update_rate_control(float buffer_fill_ratio);

    /// Flush BlipBuffer at end of frame (call after step_frame loop).
    void end_audio_frame();

  private:
    void init_filters();
    void clock_frame_counter();
    void clock_quarter_frame();
    void clock_half_frame();
    void clock_dmc();
    float mix() const;
    StereoSample mix_stereo() const;
    float filter(float sample);
    void emit_sample();
    float tpdf_dither();

    AudioSettings settings_;

    PulseChannel pulse1_;
    PulseChannel pulse2_;
    TriangleChannel triangle_;
    NoiseChannel noise_;
    DmcChannel dmc_;

    const std::array<uint16_t, 16>* noise_period_table_ = &kNoisePeriodNtsc;
    const std::array<uint16_t, 16>* dmc_rate_table_ = &kDmcRateNtsc;
    const FrameCounterTiming* frame_timing_ = &kFrameCounterNtsc;
    double cpu_clock_ = kCpuClockNtsc;

    uint32_t frame_counter_cycles_ = 0;
    bool frame_counter_mode_ = false;
    bool frame_irq_inhibit_ = false;
    bool frame_irq_pending_ = false;

    // First-order filter chain: LP (14 kHz) -> HP1 -> HP2
    AudioFilter lp_filter_;
    AudioFilter hp_filter1_;
    AudioFilter hp_filter2_;

    // Second-order biquad filter chain (Enhanced mode)
    BiquadFilter biquad_lp_;
    BiquadFilter biquad_hp1_;
    BiquadFilter biquad_hp2_;

    // Mixer lookup tables
    MixerTables mixer_;

    // Cubic Hermite interpolation state
    double cycle_accumulator_ = 0.0;
    double cycles_per_sample_ = kCpuClockNtsc / 48000.0;
    double base_cycles_per_sample_ = kCpuClockNtsc / 48000.0;
    int sample_rate_ = 48000;
    std::array<float, 4> sample_history_{};
    float current_mix_ = 0.0f;

    // BlipBuffer resampling state
    BlipBuffer blip_buffer_;
    float prev_blip_mix_ = 0.0f;
    uint32_t blip_cycle_offset_ = 0;

    // Output ring buffer
    RingBuffer<float> output_ring_{16384};

    // Legacy staging buffer for output_buffer() compat
    mutable std::vector<float> staging_buffer_;

    // DMC CPU stall cycles (4 per memory read)
    uint32_t dmc_stall_cycles_ = 0;

    // Dynamic rate control
    double drc_rate_ = 1.0;

    // TPDF dithering PRNG state
    uint32_t dither_state_ = 1;
    float prev_dither_random_ = 0.0f;

    MemoryReader memory_reader_;
    ExpansionAudioSource expansion_audio_;
};

} // namespace mapperbus::core
