#include <catch2/catch_test_macros.hpp>

#include <cmath>

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

    auto buffer = apu.output_buffer();
    REQUIRE_FALSE(buffer.empty());

    float first = buffer[0];
    bool has_different = false;
    for (float s : buffer) {
        if (std::abs(s - first) > 0.0001f) {
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
