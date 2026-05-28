#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>

#include "core/apu/apu.hpp"
#include "core/apu/audio_settings.hpp"
#include "core/apu/blip_buffer.hpp"

using namespace mapperbus::core;

TEST_CASE("AudioSettings defaults", "[audio]") {
    AudioSettings s;
    REQUIRE(s.sample_rate == 96000);
    REQUIRE(s.buffer_size_samples == 2048);
    REQUIRE(s.resampling == ResamplingMode::BlipBuffer);
    REQUIRE(s.filter_mode == FilterMode::Unfiltered);
    REQUIRE(s.filter_profile == FilterProfile::NES);
    REQUIRE(s.stereo_mode == StereoMode::Mono);
    REQUIRE_FALSE(s.dithering_enabled);
    REQUIRE(s.expansion_mixing == ExpansionMixingMode::SimpleSum);
    REQUIRE(s.drc_target_fill_ratio == 0.5f);
    REQUIRE(s.drc_deadzone == 0.05f);
    REQUIRE(s.drc_rate_adjustment == 0.005);
}

TEST_CASE("APU settings constructor", "[audio]") {
    AudioSettings s;
    s.sample_rate = 96000;
    s.resampling = ResamplingMode::CubicHermite;
    Apu apu(s);
    REQUIRE(apu.settings().sample_rate == 96000);
    REQUIRE(apu.settings().resampling == ResamplingMode::CubicHermite);
}

TEST_CASE("APU default constructor backward compat", "[audio]") {
    Apu apu;
    REQUIRE(apu.settings().sample_rate == 96000);
    REQUIRE(apu.settings().resampling == ResamplingMode::BlipBuffer);
}

TEST_CASE("Sample rate affects sample count", "[audio]") {
    AudioSettings s;
    s.resampling = ResamplingMode::CubicHermite;

    s.sample_rate = 48000;
    Apu apu48(s);
    s.sample_rate = 96000;
    Apu apu96(s);

    for (auto* apu : {&apu48, &apu96}) {
        apu->write_register(0x4015, 0x01);
        apu->write_register(0x4000, 0xBF);
        apu->write_register(0x4002, 0xFE);
        apu->write_register(0x4003, 0x08);
    }

    apu48.step(5000);
    apu96.step(5000);

    std::array<float, 2048> buf48{};
    std::array<float, 2048> buf96{};
    const std::size_t count48 = apu48.drain_samples(buf48.data(), buf48.size());
    const std::size_t count96 = apu96.drain_samples(buf96.data(), buf96.size());

    REQUIRE(count48 > 0);
    REQUIRE(count96 > 0);
    double ratio = static_cast<double>(count96) / static_cast<double>(count48);
    REQUIRE(ratio > 1.5);
    REQUIRE(ratio < 2.5);
}

TEST_CASE("BlipBuffer mode produces output", "[audio][blip]") {
    AudioSettings s;
    s.resampling = ResamplingMode::BlipBuffer;
    Apu apu(s);

    apu.write_register(0x4015, 0x01);
    apu.write_register(0x4000, 0xBF);
    apu.write_register(0x4002, 0xFE);
    apu.write_register(0x4003, 0x08);

    apu.step(30000);
    apu.end_audio_frame();

    float samples[2048];
    size_t n = apu.drain_samples(samples, 2048);
    REQUIRE(n > 0);

    bool has_nonzero = false;
    for (size_t i = 0; i < n; ++i) {
        if (std::abs(samples[i]) > 0.001f) {
            has_nonzero = true;
            break;
        }
    }
    REQUIRE(has_nonzero);
}

TEST_CASE("Biquad lowpass attenuates high frequencies", "[audio][filter]") {
    auto lp = BiquadFilter::butterworth_lowpass(14000.0f, 48000.0f);

    float max_output = 0.0f;
    for (int i = 0; i < 2000; ++i) {
        float input = std::sin(2.0f * std::numbers::pi_v<float> * 20000.0f * static_cast<float>(i) /
                               48000.0f);
        float output = lp.apply(input);
        if (i > 100 && std::abs(output) > max_output) {
            max_output = std::abs(output);
        }
    }
    REQUIRE(max_output < 0.5f);
}

TEST_CASE("Biquad lowpass passes low frequencies", "[audio][filter]") {
    auto lp = BiquadFilter::butterworth_lowpass(14000.0f, 48000.0f);

    float max_output = 0.0f;
    for (int i = 0; i < 2000; ++i) {
        float input =
            std::sin(2.0f * std::numbers::pi_v<float> * 1000.0f * static_cast<float>(i) / 48000.0f);
        float output = lp.apply(input);
        if (i > 100 && std::abs(output) > max_output) {
            max_output = std::abs(output);
        }
    }
    REQUIRE(max_output > 0.8f);
}

TEST_CASE("Biquad highpass removes DC", "[audio][filter]") {
    auto hp = BiquadFilter::butterworth_highpass(37.0f, 48000.0f);

    float output = 0.0f;
    for (int i = 0; i < 50000; ++i) {
        output = hp.apply(0.5f);
    }
    REQUIRE(std::abs(output) < 0.01f);
}

TEST_CASE("Stereo channel separation", "[audio][stereo]") {
    AudioSettings s;
    s.stereo_mode = StereoMode::PseudoStereo;
    s.resampling = ResamplingMode::CubicHermite;
    Apu apu(s);

    apu.write_register(0x4015, 0x01);
    apu.write_register(0x4000, 0xBF);
    apu.write_register(0x4002, 0xFE);
    apu.write_register(0x4003, 0x08);

    apu.step(5000);

    float samples[4096];
    size_t n = apu.drain_samples(samples, 4096);
    REQUIRE(n > 0);
    REQUIRE(n % 2 == 0);

    float left_energy = 0.0f;
    float right_energy = 0.0f;
    for (size_t i = 0; i < n; i += 2) {
        left_energy += std::abs(samples[i]);
        right_energy += std::abs(samples[i + 1]);
    }

    if (left_energy > 0.01f) {
        REQUIRE(left_energy > right_energy);
    }
}

TEST_CASE("Dithering adds noise to silence", "[audio][dither]") {
    AudioSettings s;
    s.dithering_enabled = true;
    s.resampling = ResamplingMode::CubicHermite;
    Apu apu(s);

    apu.step(5000);

    float samples[2048];
    size_t n = apu.drain_samples(samples, 2048);

    bool has_nonzero = false;
    float sum = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        sum += samples[i];
        if (samples[i] != 0.0f)
            has_nonzero = true;
    }

    if (n > 0) {
        REQUIRE(has_nonzero);
        float mean = sum / static_cast<float>(n);
        REQUIRE(std::abs(mean) < 0.001f);
    }
}

TEST_CASE("Stereo dither is centered between channels", "[audio][dither][stereo]") {
    AudioSettings s;
    s.dithering_enabled = true;
    s.stereo_mode = StereoMode::PseudoStereo;
    s.resampling = ResamplingMode::CubicHermite;
    Apu apu(s);

    apu.step(5000);

    float samples[4096];
    size_t n = apu.drain_samples(samples, 4096);
    REQUIRE(n > 0);
    REQUIRE(n % 2 == 0);

    for (size_t i = 0; i < n; i += 2) {
        REQUIRE(samples[i] == samples[i + 1]);
    }
}

TEST_CASE("BlipBuffer sample count accuracy", "[audio][blip]") {
    AudioSettings s;
    s.sample_rate = 48000;
    s.resampling = ResamplingMode::BlipBuffer;
    Apu apu(s);

    apu.write_register(0x4015, 0x01);
    apu.write_register(0x4000, 0xBF);
    apu.write_register(0x4002, 0xFE);
    apu.write_register(0x4003, 0x08);

    uint32_t frame_cycles = 29781;
    apu.step(frame_cycles);
    apu.end_audio_frame();

    float samples[2048];
    size_t n = apu.drain_samples(samples, 2048);

    double expected = static_cast<double>(frame_cycles) / (kCpuClockNtsc / 48000.0);
    REQUIRE(n > static_cast<size_t>(expected * 0.9));
    REQUIRE(n < static_cast<size_t>(expected * 1.1));
}

TEST_CASE("BlipBuffer rate refresh preserves fractional carry", "[audio][blip]") {
    BlipBuffer buffer;
    buffer.set_rates(10.0, 4.0);

    buffer.end_frame(1);
    REQUIRE(buffer.samples_available() == 0);

    buffer.set_rates(10.0, 4.0);
    buffer.end_frame(2);
    REQUIRE(buffer.samples_available() == 1);
}
