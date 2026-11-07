#include <array>
#include <catch2/catch_test_macros.hpp>

#include "frontends/sdl3/sdl3_audio.hpp"
#include "frontends/sdl3/sdl3_microphone.hpp"

TEST_CASE("SDL recording opens, polls and closes a dummy input without hardware",
          "[sdl3][microphone]") {
    mapperbus::frontend::Sdl3Microphone microphone;
    auto devices = microphone.devices();
    REQUIRE(devices);
    REQUIRE_FALSE(devices->empty());
    REQUIRE(microphone.start(""));
    std::array<float, 2048> samples{};
    for (int index = 0; index < 3; ++index) {
        auto read = microphone.read(samples);
        REQUIRE(read);
        CHECK(*read <= samples.size());
    }
    REQUIRE(microphone.discard());
    microphone.stop();
    microphone.stop();
    CHECK_FALSE(microphone.read(samples));
    REQUIRE(microphone.start(devices->front()));
    CHECK_FALSE(microphone.start("MapperBus missing test input"));
    CHECK_FALSE(microphone.read(samples));
}

TEST_CASE("SDL playback clear removes queued microphone sound while paused", "[sdl3][microphone]") {
    mapperbus::frontend::Sdl3Audio playback;
    REQUIRE(playback.initialize(48000, 256, 2));
    playback.pause();
    std::array<float, 4096> sound{};
    sound.fill(0.25F);
    playback.queue_samples(sound);
    CHECK(playback.queued_samples() >= 2048);
    playback.clear();
    CHECK(playback.queued_samples() == 0);
    playback.resume();
    playback.shutdown();
    playback.clear();
}
