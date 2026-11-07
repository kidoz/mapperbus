#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>

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
} // namespace

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
