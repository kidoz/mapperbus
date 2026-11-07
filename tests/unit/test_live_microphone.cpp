#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <vector>

#include "../support/test_rom.hpp"
#include "app/emulation_session.hpp"
#include "app/live_microphone.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "platform/audio/null_audio.hpp"
#include "platform/video/null_video.hpp"

using namespace mapperbus;

namespace {
class Capture final : public platform::MicrophoneBackend {
  public:
    core::Result<std::vector<std::string>> devices() override {
        return std::vector<std::string>{"Test input"};
    }
    core::Result<void> start(std::string_view name) override {
        ++starts;
        device = name;
        recording = true;
        if (fail_start)
            return std::unexpected("Permission denied");
        return {};
    }
    void stop() override {
        recording = false;
        ++stops;
    }
    core::Result<std::size_t> read(std::span<float> samples) override {
        if (fail_read)
            return std::unexpected("Device disconnected");
        samples[0] = level;
        samples[1] = -level;
        return empty ? 0 : 2;
    }
    core::Result<void> discard() override {
        ++discards;
        return {};
    }
    int starts = 0, stops = 0, discards = 0;
    bool recording = false, fail_start = false, fail_read = false, empty = false;
    float level = 0.1F;
    std::string device;
};

class Input final : public platform::InputBackend {
  public:
    void poll() override {}
    bool is_button_pressed(int, core::Button) const override {
        return false;
    }
    bool is_action_pressed(int, platform::InputAction action) const override {
        return held && action == platform::InputAction::Microphone;
    }
    bool should_quit() const override {
        return false;
    }
    bool held = false;
};

class Playback final : public platform::NullAudio {
  public:
    void queue_samples(std::span<const float> samples) override {
        last.assign(samples.begin(), samples.end());
    }
    void clear() override {
        ++clears;
        last.clear();
    }
    int queued_samples() const override {
        return queued;
    }
    std::vector<float> last;
    int clears = 0;
    int queued = 0;
};
} // namespace

TEST_CASE("Microphone playback is optional and independent of the activity gate",
          "[microphone][mixing]") {
    app::LiveMicrophone mic;
    auto backend = std::make_unique<Capture>();
    auto* capture = backend.get();
    mic.set_backend(std::move(backend));
    app::LiveMicrophoneSettings settings{.enabled = true, .device = {}};
    mic.configure(settings);
    REQUIRE(mic.poll(true));
    std::array<float, 4> game{0.9F, -0.9F, 0.5F, -0.5F};
    const auto original = game;
    mic.mix_into(game, 16000, 1);
    CHECK(game == original);
    settings.audio_enabled = true;
    settings.volume_percent = 50;
    mic.configure(settings);
    capture->level = 4;
    REQUIRE(mic.poll(true));
    mic.mix_into(game, 16000, 1);
    CHECK(game == std::array<float, 4>{1, -1, 0.5F, -0.5F});
    settings.volume_percent = 0;
    mic.configure(settings);
    REQUIRE(mic.poll(true)); // muting audio never mutes detection
    game = original;
    mic.mix_into(game, 16000, 1);
    CHECK(game == original);
    settings.volume_percent = 100;
    settings.sensitivity = 1;
    mic.configure(settings);
    capture->level = 0.01F;
    CHECK_FALSE(mic.poll(true)); // quiet input is audible below the gate threshold
    std::array<float, 4> stereo{};
    mic.mix_into(stereo, 16000, 2);
    CHECK(stereo[0] == Catch::Approx(0.01F));
    CHECK(stereo[1] == stereo[0]);
    CHECK(stereo[2] == Catch::Approx(-0.01F));
    CHECK(stereo[3] == stereo[2]);
    capture->level = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(mic.poll(true));
    std::array<float, 2> clean{};
    mic.mix_into(clean, 16000, 1);
    CHECK(clean == std::array<float, 2>{0, 0});
}

TEST_CASE("Microphone resampling is bounded and never replays discarded sound",
          "[microphone][mixing]") {
    app::LiveMicrophone mic;
    auto backend = std::make_unique<Capture>();
    auto* capture = backend.get();
    capture->level = 0.5F;
    mic.set_backend(std::move(backend));
    mic.configure({.enabled = true, .device = {}, .audio_enabled = true, .volume_percent = 100});
    REQUIRE(mic.poll(true));
    std::array<float, 2> first{}, second{}, empty{};
    mic.mix_into(first, 32000, 1);
    mic.mix_into(second, 32000, 1);
    mic.mix_into(empty, 32000, 1);
    CHECK(first[0] == Catch::Approx(0.5F));
    CHECK(first[1] == Catch::Approx(0));
    CHECK(second == std::array<float, 2>{-0.5F, -0.5F});
    CHECK(empty == std::array<float, 2>{0, 0});
    // More than 128 ms without playback replaces the backlog with fresh samples.
    for (int i = 0; i < 1024; ++i)
        (void)mic.poll(true);
    capture->level = 0.25F;
    REQUIRE(mic.poll(true));
    std::array<float, 2> latest{};
    mic.mix_into(latest, 8000, 1);
    CHECK(latest == std::array<float, 2>{0.25F, 0});
    for (int action = 0; action < 4; ++action) {
        REQUIRE(mic.poll(true));
        if (action == 0)
            mic.discard();
        if (action == 1)
            mic.suspend();
        if (action == 2)
            mic.retry();
        if (action == 3) {
            capture->fail_read = true;
            CHECK_FALSE(mic.poll(true));
        }
        std::array<float, 4> silence{};
        mic.mix_into(silence, 96000, 1);
        CHECK(std::ranges::all_of(silence, [](float x) { return x == 0; }));
    }
}

TEST_CASE("Session mixing changes playback without changing machine snapshots",
          "[microphone][mixing][session]") {
    core::register_builtin_mappers();
    const auto rom = tests::write_visible_nrom_test_rom("microphone-mixing");
    auto playback = std::make_unique<Playback>();
    auto* output = playback.get();
    app::EmulationSession session(
        std::make_unique<platform::NullVideo>(), std::move(playback), std::make_unique<Input>());
    auto backend = std::make_unique<Capture>();
    auto* capture = backend.get();
    capture->level = 0.5F;
    session.set_microphone_backend(std::move(backend));
    app::LiveMicrophoneSettings settings{.enabled = true, .device = {}};
    session.configure_live_microphone(settings);
    session.set_controller_wiring(core::ControllerWiring::RfFamicom);
    REQUIRE(session.load_rom(rom.string()));
    app::StateStorageSettings storage;
    storage.directory = rom.parent_path() / (rom.stem().string() + "-states");
    session.set_state_storage(storage);
    REQUIRE(session.save_state(0));
    REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    const auto dry = output->last;
    const auto machine = session.emulator().save_state();
    REQUIRE(session.load_state(0));
    settings.audio_enabled = true;
    settings.volume_percent = 50;
    session.configure_live_microphone(settings);
    REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    REQUIRE(output->last.size() == dry.size());
    REQUIRE_FALSE(dry.empty());
    CHECK(output->last[0] == Catch::Approx(std::clamp(dry[0] + 0.25F, -1.0F, 1.0F)));
    CHECK(output->last != dry);
    CHECK(session.emulator().save_state() == machine);
    const auto check_clear = [&](auto action) {
        const auto clears = output->clears;
        action();
        CHECK(output->clears > clears);
    };
    check_clear([&] { session.set_paused(true); });
    session.set_paused(false);
    REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    check_clear([&] { REQUIRE(session.load_state(0)); });
    REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    capture->fail_read = true;
    check_clear([&] { REQUIRE(session.tick() == app::TickResult::FrameAdvanced); });
    capture->fail_read = false;
    session.live_microphone().retry();
    REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    settings.audio_enabled = false;
    check_clear([&] { session.configure_live_microphone(settings); });
    CHECK(capture->recording); // audio off leaves the detector's device running
    std::filesystem::remove_all(storage.directory);
    std::filesystem::remove(rom);
}

TEST_CASE("Live capture gates finite PCM with sensitivity and hysteresis",
          "[microphone][capture]") {
    app::LiveMicrophone mic;
    auto backend = std::make_unique<Capture>();
    auto* capture = backend.get();
    mic.set_backend(std::move(backend));
    CHECK_FALSE(mic.poll(true));
    CHECK(capture->starts == 0);
    mic.configure({.enabled = true, .device = "Test input"});
    CHECK_FALSE(mic.poll(false));
    CHECK(capture->starts == 0);
    CHECK(mic.poll(true));
    CHECK(capture->device == "Test input");
    capture->level = 0.015F;
    CHECK(mic.poll(true)); // lower release threshold prevents chatter
    capture->level = 0.005F;
    CHECK_FALSE(mic.poll(true));
    capture->level = 0.015F;
    CHECK_FALSE(mic.poll(true));
    mic.configure({.enabled = true, .device = {}, .sensitivity = 100});
    CHECK(mic.poll(true));
    mic.configure({.enabled = true, .device = {}, .sensitivity = 1});
    CHECK_FALSE(mic.poll(true));
    capture->level = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(mic.poll(true));
    capture->level = std::numeric_limits<float>::infinity();
    CHECK_FALSE(mic.poll(true));
    capture->empty = true;
    CHECK_FALSE(mic.poll(true));
    mic.discard();
    CHECK(capture->discards == 1);
    mic.suspend();
    CHECK_FALSE(capture->recording);
}

TEST_CASE("Capture errors close devices and wait for explicit retry", "[microphone][capture]") {
    app::LiveMicrophone mic;
    auto backend = std::make_unique<Capture>();
    auto* capture = backend.get();
    mic.set_backend(std::move(backend));
    mic.configure({.enabled = true, .device = {}});
    capture->fail_start = true;
    CHECK_FALSE(mic.poll(true));
    CHECK_FALSE(capture->recording);
    CHECK(mic.status().contains("Permission denied"));
    CHECK_FALSE(mic.poll(true));
    CHECK(capture->starts == 1);
    capture->fail_start = false;
    mic.retry();
    CHECK(mic.poll(true));
    capture->fail_read = true;
    CHECK_FALSE(mic.poll(true));
    CHECK_FALSE(capture->recording);
    CHECK(mic.status().contains("disconnected"));
    const auto starts = capture->starts;
    CHECK_FALSE(mic.poll(true));
    CHECK(capture->starts == starts);
    capture->fail_read = false;
    mic.retry();
    CHECK(mic.poll(true));
    mic.configure({.enabled = false, .device = {}});
    CHECK_FALSE(capture->recording);
    app::LiveMicrophone unavailable;
    unavailable.configure({.enabled = true, .device = {}});
    CHECK_FALSE(unavailable.poll(true));
    CHECK_FALSE(unavailable.devices());
    CHECK(unavailable.status().contains("unavailable"));
}

TEST_CASE("Session capture closes on pause, snapshots, wiring changes and stop",
          "[microphone][session][capture]") {
    core::register_builtin_mappers();
    const auto rom = tests::write_visible_nrom_test_rom("live-microphone");
    auto input = std::make_unique<Input>();
    auto* buttons = input.get();
    app::EmulationSession session(std::make_unique<platform::NullVideo>(),
                                  std::make_unique<platform::NullAudio>(),
                                  std::move(input));
    auto backend = std::make_unique<Capture>();
    auto* capture = backend.get();
    session.set_microphone_backend(std::move(backend));
    session.configure_live_microphone({.enabled = true, .device = {}});
    CHECK(capture->starts == 0);
    REQUIRE(session.load_rom(rom.string()));
    REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    CHECK(capture->starts == 0); // NES wiring never opens capture
    session.set_controller_wiring(core::ControllerWiring::RfFamicom);
    REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    CHECK(session.emulator().microphone_active());
    CHECK(capture->recording);
    session.set_paused(true);
    CHECK_FALSE(capture->recording);
    const auto starts = capture->starts;
    REQUIRE(session.step_frame() == app::TickResult::FrameAdvanced);
    CHECK(capture->starts == starts); // manual stepping while paused uses digital input only
    session.set_paused(false);
    REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    CHECK(capture->recording);
    app::StateStorageSettings storage;
    storage.directory = rom.parent_path() / (rom.stem().string() + "-states");
    session.set_state_storage(storage);
    REQUIRE(session.save_state(0));
    const auto saved = session.emulator().save_state();
    REQUIRE(session.load_state(0));
    CHECK(session.emulator().save_state() == saved);
    CHECK_FALSE(capture->recording);
    capture->level = 0;
    REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    CHECK_FALSE(session.emulator().microphone_active());
    buttons->held = true;
    capture->fail_read = true;
    REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    CHECK(session.emulator().microphone_active()); // bound input survives a capture failure
    CHECK_FALSE(capture->recording);
    capture->fail_read = false;
    session.live_microphone().retry();
    REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    CHECK(capture->recording);
    session.set_controller_wiring(core::ControllerWiring::Nes);
    CHECK_FALSE(capture->recording);
    session.request_stop();
    CHECK_FALSE(capture->recording);
    std::filesystem::remove_all(storage.directory);
    std::filesystem::remove(rom);
}
