#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace mapperbus::core {

/// Lightweight band-limited sample buffer.
/// Tracks amplitude deltas at precise cycle timestamps and reconstructs
/// alias-free output samples using windowed sinc interpolation.
class BlipBuffer {
  public:
    static constexpr int kKernelSize = 64; // Sinc kernel width in samples
    static constexpr int kPhaseBits = 8;   // Sub-sample phase resolution
    static constexpr int kPhaseCount = 1 << kPhaseBits;

    BlipBuffer();

    /// Configure the ratio of input clocks to output samples.
    void set_rates(double clock_rate, double sample_rate);

    /// Record an amplitude change at the given clock offset within the current frame.
    void add_delta(uint32_t clock_offset, float delta);

    /// End the current frame of `clock_count` input clocks.
    /// Makes output samples available for reading.
    void end_frame(uint32_t clock_count);

    /// Number of output samples available to read.
    int samples_available() const;

    /// Read output samples into the destination buffer. Returns number read.
    int read_samples(float* dest, int max_samples);

    /// Reset all state.
    void reset();

  private:
    void init_kernel();

    // Windowed sinc kernel table [phase][tap]
    std::array<std::array<float, kKernelSize>, kPhaseCount> kernel_{};

    // Accumulation buffer (ring buffer of output samples + fractional)
    std::vector<float> buffer_;
    int buffer_size_ = 0;
    int sample_offset_ = 0; // Read position
    int sample_count_ = 0;  // Available samples

    double clocks_per_sample_ = 1.0;
    double fraction_ = 0.0; // Fractional clock accumulator

    // Running integrator: buffer stores differentiated BLEP;
    // read_samples() integrates via this accumulator.
    float integrator_ = 0.0f;
};

} // namespace mapperbus::core
