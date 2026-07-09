#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <vector>

#include "core/apu/apu.hpp"

using namespace mapperbus::core;

TEST_CASE("APU region switching", "[apu]") {
    SECTION("NTSC") {
        Apu apu;
        apu.set_region(Region::NTSC);
        apu.write_register(0x4015, 0x08);
        apu.write_register(0x400E, 0x08);
        apu.step(1);
    }
    SECTION("PAL") {
        Apu apu;
        apu.set_region(Region::PAL);
        apu.write_register(0x4015, 0x08);
        apu.write_register(0x400E, 0x08);
        apu.step(1);
    }
}

TEST_CASE("DMC direct load", "[apu]") {
    Apu apu;
    apu.write_register(0x4015, 0x10);
    apu.write_register(0x4011, 0x40);
    apu.step(100);
}

TEST_CASE("DMC output level clamped", "[apu]") {
    Apu apu;
    apu.write_register(0x4011, 0x7F);
    apu.step(1);
    apu.write_register(0x4011, 0x00);
    apu.step(1);
}

TEST_CASE("DMC rate table selection", "[apu]") {
    SECTION("NTSC rate index 0 = 428") {
        Apu apu;
        apu.set_region(Region::NTSC);
        apu.write_register(0x4015, 0x10);
        apu.write_register(0x4010, 0x00);
        apu.step(1);
    }
    SECTION("PAL rate index 0 = 398") {
        Apu apu;
        apu.set_region(Region::PAL);
        apu.write_register(0x4015, 0x10);
        apu.write_register(0x4010, 0x00);
        apu.step(1);
    }
}

TEST_CASE("APU status register", "[apu]") {
    SECTION("read shows length counter state") {
        Apu apu;
        apu.write_register(0x4015, 0x01);
        apu.write_register(0x4000, 0x30);
        apu.write_register(0x4003, 0x08);
        REQUIRE((apu.read_register(0x4015) & 0x01) != 0);
    }
    SECTION("disable clears length counter") {
        Apu apu;
        apu.write_register(0x4015, 0x01);
        apu.write_register(0x4003, 0x08);
        apu.write_register(0x4015, 0x00);
        REQUIRE((apu.read_register(0x4015) & 0x01) == 0);
    }
}

TEST_CASE("Pulse channel produces samples", "[apu]") {
    Apu apu;
    apu.write_register(0x4015, 0x01);
    apu.write_register(0x4000, 0xBF);
    apu.write_register(0x4002, 0xFE);
    apu.write_register(0x4003, 0x08);

    apu.step(5000);
    apu.end_audio_frame();

    std::array<float, 2048> buffer{};
    const std::size_t count = apu.drain_samples(buffer.data(), buffer.size());
    REQUIRE(count > 0);

    float first = buffer[0];
    bool has_different = false;
    for (std::size_t i = 0; i < count; ++i) {
        if (std::abs(buffer[i] - first) > 0.0001f) {
            has_different = true;
            break;
        }
    }
    REQUIRE(has_different);
}

TEST_CASE("High-pass filter removes DC", "[apu][filter]") {
    AudioFilter filter;
    filter.alpha = 0.996f;
    filter.is_highpass = true;

    float output = 0.0f;
    for (int i = 0; i < 10000; ++i) {
        output = filter.apply(0.5f);
    }
    REQUIRE(std::abs(output) < 0.01f);
}

TEST_CASE("IIR filters snap near-denormal state to zero", "[apu][filter]") {
    SECTION("first-order filter") {
        AudioFilter filter;
        filter.alpha = 0.5f;
        filter.is_highpass = true;
        filter.prev_output = 1.0e-20f;

        REQUIRE(filter.apply(0.0f) == 0.0f);
        REQUIRE(filter.prev_output == 0.0f);
    }

    SECTION("biquad filter") {
        BiquadFilter filter;
        filter.b0 = 1.0f;
        filter.z1 = 1.0e-20f;
        filter.z2 = -1.0e-20f;

        REQUIRE(filter.apply(0.0f) == 0.0f);
        REQUIRE(filter.z1 == 0.0f);
        REQUIRE(filter.z2 == 0.0f);
    }
}

TEST_CASE("High-pass filter passes AC", "[apu][filter]") {
    AudioFilter filter;
    filter.alpha = 0.996f;
    filter.is_highpass = true;

    float max_output = 0.0f;
    for (int i = 0; i < 1000; ++i) {
        float input = (i % 2 == 0) ? 0.5f : -0.5f;
        float output = filter.apply(input);
        if (std::abs(output) > max_output) {
            max_output = std::abs(output);
        }
    }
    REQUIRE(max_output > 0.3f);
}

TEST_CASE("Noise period tables differ between regions", "[apu]") {
    REQUIRE(kNoisePeriodNtsc[8] == 202);
    REQUIRE(kNoisePeriodPal[8] == 188);
}

TEST_CASE("DMC rate tables", "[apu]") {
    REQUIRE(kDmcRateNtsc[0] == 428);
    REQUIRE(kDmcRatePal[0] == 398);
    REQUIRE(kDmcRateNtsc[15] == 54);
    REQUIRE(kDmcRatePal[15] == 50);
}

// --- Accuracy regression tests (validated against NESdev hardware behavior) ---

namespace {

AudioSettings accuracy_settings() {
    AudioSettings s;
    s.sample_rate = 96000;
    s.resampling = ResamplingMode::BlipBuffer;
    s.filter_mode = FilterMode::Unfiltered;
    return s;
}

std::vector<float> capture_seconds(Apu& apu, int frames) {
    std::vector<float> samples;
    std::vector<float> chunk(8192);
    for (int f = 0; f < frames; ++f) {
        apu.step(29780);
        apu.end_audio_frame();
        std::size_t n = 0;
        while ((n = apu.drain_samples(chunk.data(), chunk.size())) > 0) {
            samples.insert(
                samples.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(n));
        }
    }
    return samples;
}

double rising_edge_freq(const std::vector<float>& samples) {
    float lo = 1e9f;
    float hi = -1e9f;
    for (float s : samples) {
        lo = std::min(lo, s);
        hi = std::max(hi, s);
    }
    const float mid = 0.5f * (lo + hi);
    int rising = 0;
    long first = -1;
    long last = -1;
    for (std::size_t i = 1; i < samples.size(); ++i) {
        if (samples[i - 1] < mid && samples[i] >= mid) {
            ++rising;
            if (first < 0)
                first = static_cast<long>(i);
            last = static_cast<long>(i);
        }
    }
    if (rising <= 2)
        return 0.0;
    return static_cast<double>(rising - 1) / (static_cast<double>(last - first) / 96000.0);
}

// Fundamental via autocorrelation peak: robust for staircase waveforms
// (triangle, DPCM) where blip ringing makes midpoint edge counts unstable.
double autocorr_freq(const std::vector<float>& samples, int min_lag, int max_lag) {
    const std::size_t n = std::min<std::size_t>(samples.size(), 48000);
    double mean = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        mean += static_cast<double>(samples[i]);
    mean /= static_cast<double>(n);
    double best = -1e18;
    int best_lag = 0;
    for (int lag = min_lag; lag <= max_lag; ++lag) {
        double acc = 0.0;
        for (std::size_t i = 0; i + static_cast<std::size_t>(lag) < n; ++i) {
            acc += (static_cast<double>(samples[i]) - mean) *
                   (static_cast<double>(samples[i + static_cast<std::size_t>(lag)]) - mean);
        }
        if (acc > best) {
            best = acc;
            best_lag = lag;
        }
    }
    return best_lag > 0 ? 96000.0 / best_lag : 0.0;
}

float peak_to_peak(const std::vector<float>& samples) {
    float lo = 1e9f;
    float hi = -1e9f;
    for (float s : samples) {
        lo = std::min(lo, s);
        hi = std::max(hi, s);
    }
    return hi - lo;
}

} // namespace

TEST_CASE("Pulse pitch matches hardware formula", "[apu][accuracy]") {
    // f = CPU / (16 * (t + 1)): t = 253 -> 440.4 Hz. Pulse timers tick every
    // other CPU cycle; clocking them at CPU rate plays one octave high.
    Apu apu(accuracy_settings());
    apu.write_register(0x4015, 0x01);
    apu.write_register(0x4017, 0x40);
    apu.write_register(0x4000, 0xBF);
    apu.write_register(0x4002, 0xFD);
    apu.write_register(0x4003, 0x00);
    const double freq = rising_edge_freq(capture_seconds(apu, 62));
    REQUIRE(freq > 430.0);
    REQUIRE(freq < 450.0);
}

TEST_CASE("Negate sweep with shift 0 does not mute", "[apu][accuracy]") {
    // $4001=$08 is the standard "disable sweep" idiom; negative sweep
    // targets must never trip the >$7FF mute (unsigned wrap did exactly
    // that and silenced pulse 1 in any game using it).
    Apu apu(accuracy_settings());
    apu.write_register(0x4015, 0x01);
    apu.write_register(0x4017, 0x40);
    apu.write_register(0x4000, 0xBF);
    apu.write_register(0x4001, 0x08);
    apu.write_register(0x4002, 0xFD);
    apu.write_register(0x4003, 0x00);
    REQUIRE(peak_to_peak(capture_seconds(apu, 31)) > 0.05f);
}

TEST_CASE("DPCM playback rate is 8 timer clocks per byte", "[apu][accuracy]") {
    // Uniform 0xF0 bytes produce a wave with an exact 8-bit-clock period:
    // rate index 0 (428 cycles/bit) -> 1789773 / (428 * 8) = 522.7 Hz.
    // A reload that consumes its own timer clock (9 per byte) reads 464.6.
    Apu apu(accuracy_settings());
    apu.set_memory_reader([](Address) -> Byte { return 0xF0; });
    apu.write_register(0x4010, 0x40);
    apu.write_register(0x4011, 0x40);
    apu.write_register(0x4012, 0x00);
    apu.write_register(0x4013, 0x01);
    apu.write_register(0x4017, 0x40);
    apu.write_register(0x4015, 0x10);
    std::vector<float> samples = capture_seconds(apu, 62);
    REQUIRE(samples.size() > 30000);
    // 522.7 Hz -> lag ~184 samples at 96 kHz; the 9-clock bug reads 464.6.
    const double freq = autocorr_freq(samples, 100, 400);
    REQUIRE(freq > 510.0);
    REQUIRE(freq < 535.0);
}

TEST_CASE("PseudoStereo BlipBuffer output varies within a frame", "[apu][accuracy]") {
    // Deriving stereo from post-frame channel state degenerates into one
    // constant pair per frame (a 60 Hz staircase); per-cycle stereo deltas
    // must yield a real waveform with the pulse 1 pan ratio.
    AudioSettings s = accuracy_settings();
    s.stereo_mode = StereoMode::PseudoStereo;
    Apu apu(s);
    apu.write_register(0x4015, 0x01);
    apu.write_register(0x4017, 0x40);
    apu.write_register(0x4000, 0xBF);
    apu.write_register(0x4002, 0xFD);
    apu.write_register(0x4003, 0x00);
    const std::vector<float> interleaved = capture_seconds(apu, 31);
    REQUIRE(interleaved.size() % 2 == 0);
    std::vector<float> left;
    std::vector<float> right;
    for (std::size_t i = 0; i + 1 < interleaved.size(); i += 2) {
        left.push_back(interleaved[i]);
        right.push_back(interleaved[i + 1]);
    }
    // ~1600 samples per frame at 96 kHz: a slice inside one frame must vary.
    int changes = 0;
    for (std::size_t i = 3000; i < 4000 && i < left.size(); ++i) {
        if (left[i] != left[i - 1])
            ++changes;
    }
    REQUIRE(changes > 100);
    const float ratio = peak_to_peak(left) / peak_to_peak(right);
    REQUIRE(ratio > 2.5f); // kPanPulse1 = 0.75 / 0.25
    REQUIRE(ratio < 3.5f);
    const double freq = rising_edge_freq(left);
    REQUIRE(freq > 430.0);
    REQUIRE(freq < 450.0);
}

TEST_CASE("DRC rate changes do not introduce impulses in BlipBuffer output", "[apu][accuracy]") {
    // Re-rating the BlipBuffer mid-stream (the former DRC behavior) left
    // in-flight kernel energy misaligned with the read pointer, producing
    // reconstruction crackle. With DRC no longer touching the BlipBuffer,
    // sweeping the buffer fill across the deadzone must not create any
    // sample-to-sample step larger than what a clean, rate-stable capture
    // already produces.
    Apu apu(accuracy_settings());
    apu.write_register(0x4015, 0x01);
    apu.write_register(0x4017, 0x40);
    apu.write_register(0x4000, 0xBF);
    apu.write_register(0x4002, 0xFD);
    apu.write_register(0x4003, 0x00);

    // Baseline: a clean capture with no DRC activity.
    const std::vector<float> clean = capture_seconds(apu, 62);
    float clean_max_step = 0.0f;
    for (std::size_t i = 1; i < clean.size(); ++i) {
        clean_max_step = std::max(clean_max_step, std::abs(clean[i] - clean[i - 1]));
    }

    // Reset and re-capture while driving update_rate_control across the
    // deadzone every frame. The fill ratio is synthetic; only the rate
    // retuning is under test.
    apu.reset();
    apu.write_register(0x4015, 0x01);
    apu.write_register(0x4017, 0x40);
    apu.write_register(0x4000, 0xBF);
    apu.write_register(0x4002, 0xFD);
    apu.write_register(0x4003, 0x00);

    std::vector<float> driven;
    std::vector<float> chunk(8192);
    for (int f = 0; f < 62; ++f) {
        // Sweep fill ratio from below to above the deadzone and back.
        float ratio = 0.5f + 0.4f * std::sin(static_cast<float>(f) * 0.4f);
        apu.update_rate_control(ratio);
        apu.step(29780);
        apu.end_audio_frame();
        std::size_t n = 0;
        while ((n = apu.drain_samples(chunk.data(), chunk.size())) > 0) {
            driven.insert(
                driven.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(n));
        }
    }

    float driven_max_step = 0.0f;
    for (std::size_t i = 1; i < driven.size(); ++i) {
        driven_max_step = std::max(driven_max_step, std::abs(driven[i] - driven[i - 1]));
    }

    // A corrupted reconstruction produces impulses far larger than the clean
    // waveform's natural per-sample step. Allow generous headroom for the
    // legitimate phase reset at capture start.
    REQUIRE(driven_max_step <= clean_max_step * 3.0f + 0.05f);
}

TEST_CASE("Ring overflow drops oldest, not newest", "[apu][accuracy]") {
    // When the output ring fills, the newest frame must survive intact;
    // truncating it mid-frame would leave a vertical seam (a click). Fill
    // the ring past capacity with a known ramp and confirm the tail is
    // present and continuous.
    Apu apu(accuracy_settings());
    apu.write_register(0x4015, 0x01);
    apu.write_register(0x4000, 0xBF);
    apu.write_register(0x4002, 0xFE);
    apu.write_register(0x4003, 0x08);

    // Produce many frames without draining so the ring saturates and the
    // overflow path engages. ~1600 samples/frame; ring capacity is 16384.
    for (int f = 0; f < 20; ++f) {
        apu.step(29780);
        apu.end_audio_frame();
    }

    // Drain everything the ring will now give us.
    std::vector<float> out;
    std::vector<float> chunk(8192);
    std::size_t n = 0;
    while ((n = apu.drain_samples(chunk.data(), chunk.size())) > 0) {
        out.insert(out.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(n));
    }
    REQUIRE(out.size() <= 16384);

    // The final samples must be a real, varying waveform (the newest frame),
    // not a truncated constant. Check the last 100 samples vary.
    REQUIRE(out.size() > 100);
    bool tail_varies = false;
    for (std::size_t i = out.size() - 100; i < out.size(); ++i) {
        if (std::abs(out[i] - out[i - 1]) > 1e-5f) {
            tail_varies = true;
            break;
        }
    }
    REQUIRE(tail_varies);
}
