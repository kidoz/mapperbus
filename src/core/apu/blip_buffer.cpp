#include "core/apu/blip_buffer.hpp"

#include <algorithm>
#include <cstring>

namespace mapperbus::core {

BlipBuffer::BlipBuffer() {
    buffer_.resize(4096, 0.0f);
    buffer_size_ = static_cast<int>(buffer_.size());
    init_kernel();
}

void BlipBuffer::init_kernel() {
    // Generate windowed sinc kernel (derivative of band-limited step).
    // add_delta() convolves amplitude changes with this kernel, storing
    // the differentiated BLEP in the ring buffer. read_samples() integrates
    // via a running sum to reconstruct the actual waveform.
    // Uses Blackman window for good stopband attenuation.
    // Normalized so each phase sums to 1.0 (correct DC gain after integration).
    for (int phase = 0; phase < kPhaseCount; ++phase) {
        double phase_offset = static_cast<double>(phase) / kPhaseCount;
        double sum = 0.0;
        for (int tap = 0; tap < kKernelSize; ++tap) {
            double t = static_cast<double>(tap - kKernelSize / 2) + phase_offset;

            // Sinc function
            double sinc = 1.0;
            if (std::abs(t) > 1e-9) {
                double x = t * std::numbers::pi;
                sinc = std::sin(x) / x;
            }

            // Blackman window
            double window_pos = static_cast<double>(tap) / static_cast<double>(kKernelSize - 1);
            double window = 0.42 - 0.5 * std::cos(2.0 * std::numbers::pi * window_pos) +
                            0.08 * std::cos(4.0 * std::numbers::pi * window_pos);

            kernel_[phase][tap] = static_cast<float>(sinc * window);
            sum += sinc * window;
        }

        // Normalize so kernel sums to 1.0
        if (std::abs(sum) > 1e-9) {
            float inv_sum = static_cast<float>(1.0 / sum);
            for (int tap = 0; tap < kKernelSize; ++tap) {
                kernel_[phase][tap] *= inv_sum;
            }
        }
    }
}

void BlipBuffer::set_rates(double clock_rate, double sample_rate) {
    clocks_per_sample_ = clock_rate / sample_rate;
    fraction_ = 0.0;
}

void BlipBuffer::add_delta(uint32_t clock_offset, float delta) {
    if (std::abs(delta) < 1e-8f)
        return;

    // Convert clock offset to output sample position + fractional phase
    double sample_pos = static_cast<double>(clock_offset) / clocks_per_sample_;
    int output_index = static_cast<int>(sample_pos);
    double frac = sample_pos - output_index;
    int phase = static_cast<int>(frac * kPhaseCount) & (kPhaseCount - 1);

    // Scatter the delta across nearby output samples using the sinc kernel
    for (int tap = 0; tap < kKernelSize; ++tap) {
        int buf_idx = (sample_offset_ + output_index + tap - kKernelSize / 2) % buffer_size_;
        if (buf_idx < 0)
            buf_idx += buffer_size_;
        buffer_[buf_idx] += delta * kernel_[phase][tap];
    }
}

void BlipBuffer::end_frame(uint32_t clock_count) {
    // Calculate how many output samples this frame produced
    double total_samples = static_cast<double>(clock_count) / clocks_per_sample_ + fraction_;
    int new_samples = static_cast<int>(total_samples);
    fraction_ = total_samples - new_samples;
    sample_count_ += new_samples;
}

int BlipBuffer::samples_available() const {
    return sample_count_;
}

int BlipBuffer::read_samples(float* dest, int max_samples) {
    int count = std::min(max_samples, sample_count_);
    for (int i = 0; i < count; ++i) {
        int idx = (sample_offset_ + i) % buffer_size_;
        // Integrate: the buffer holds the differentiated BLEP signal;
        // accumulating produces the actual band-limited waveform.
        integrator_ += buffer_[idx];
        dest[i] = integrator_;
        buffer_[idx] = 0.0f; // Clear after reading
    }
    sample_offset_ = (sample_offset_ + count) % buffer_size_;
    sample_count_ -= count;
    return count;
}

void BlipBuffer::reset() {
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    sample_offset_ = 0;
    sample_count_ = 0;
    fraction_ = 0.0;
    integrator_ = 0.0f;
}

} // namespace mapperbus::core
