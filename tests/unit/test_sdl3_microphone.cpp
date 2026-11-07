#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>

#include "../support/test_rom.hpp"
#include "app/emulation_session.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "frontends/sdl3/sdl3_audio.hpp"
#include "frontends/sdl3/sdl3_microphone.hpp"
#include "platform/audio/null_audio.hpp"
#include "platform/input/null_input.hpp"
#include "platform/video/null_video.hpp"

namespace {
using mapperbus::platform::MicrophoneAccess;
struct PermissionState {
    MicrophoneAccess access = MicrophoneAccess::NotDetermined;
    int requests = 0;
};
class Permission final : public mapperbus::platform::MicrophonePermission {
  public:
    explicit Permission(std::shared_ptr<PermissionState> state) : state_(std::move(state)) {}
    MicrophoneAccess status() const override {
        return state_->access;
    }
    void request() override {
        ++state_->requests;
        state_->access = MicrophoneAccess::Pending;
    }

  private:
    std::shared_ptr<PermissionState> state_;
};
class BoundMicrophone final : public mapperbus::platform::NullInput {
  public:
    bool is_action_pressed(int, mapperbus::platform::InputAction action) const override {
        return action == mapperbus::platform::InputAction::Microphone;
    }
};
} // namespace

TEST_CASE("Permission gates physical capture and detects revoked access",
          "[sdl3][microphone][permission]") {
    auto state = std::make_shared<PermissionState>();
    mapperbus::frontend::Sdl3Microphone microphone(std::make_unique<Permission>(state));
    REQUIRE(microphone.devices());
    CHECK(state->requests == 0); // enumeration is never permission to record
    CHECK_FALSE(microphone.start("MapperBus missing test input"));
    CHECK(state->requests == 0);
    auto pending = microphone.start("");
    REQUIRE_FALSE(pending);
    CHECK(pending.error().contains("prompt"));
    CHECK(pending.error().contains("Retry"));
    CHECK(state->requests == 1);
    CHECK_FALSE(microphone.start(""));
    CHECK(state->requests == 1); // a pending prompt is never requested again
    state->access = MicrophoneAccess::Denied;
    auto denied = microphone.start("");
    REQUIRE_FALSE(denied);
    CHECK(denied.error().contains("System Settings"));
    CHECK(denied.error().contains("Reopen if macOS asks"));
    CHECK(denied.error().contains("Privacy & Security > Microphone"));
    state->access = MicrophoneAccess::Restricted;
    auto restricted = microphone.start("");
    REQUIRE_FALSE(restricted);
    CHECK(restricted.error().contains("restricted"));
    CHECK(restricted.error().contains("administrator"));
    CHECK(state->requests == 1);
    state->access = MicrophoneAccess::Authorized;
    REQUIRE(microphone.start(""));
    std::array<float, 32> samples{};
    REQUIRE(microphone.read(samples));
    state->access = MicrophoneAccess::Denied;
    auto revoked = microphone.read(samples);
    REQUIRE_FALSE(revoked);
    CHECK(revoked.error().contains("denied"));
    CHECK_FALSE(microphone.discard()); // revocation closed the stream
    state->access = MicrophoneAccess::Authorized;
    REQUIRE(microphone.start(""));
    state->access = MicrophoneAccess::NotDetermined;
    auto reset = microphone.discard();
    REQUIRE_FALSE(reset);
    CHECK(reset.error().contains("Retry to request access"));
    CHECK(state->requests == 1); // running capture cannot trigger another prompt
    CHECK_FALSE(microphone.read(samples));
}

TEST_CASE("Permission prompts never block frames or bound input and require Retry",
          "[sdl3][microphone][permission][session]") {
    using namespace mapperbus;
    core::register_builtin_mappers();
    const auto rom = tests::write_visible_nrom_test_rom("microphone-permission");
    auto state = std::make_shared<PermissionState>();
    app::EmulationSession session(std::make_unique<platform::NullVideo>(),
                                  std::make_unique<platform::NullAudio>(),
                                  std::make_unique<BoundMicrophone>());
    session.set_microphone_backend(
        std::make_unique<frontend::Sdl3Microphone>(std::make_unique<Permission>(state)));
    session.configure_live_microphone({.enabled = true, .device = {}, .audio_enabled = true});
    REQUIRE(session.load_rom(rom.string()));
    REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    CHECK(state->requests == 0); // NES wiring never requests access
    session.set_controller_wiring(core::ControllerWiring::RfFamicom);
    session.set_paused(true);
    REQUIRE(session.step_frame() == app::TickResult::FrameAdvanced);
    CHECK(state->requests == 0); // neither does paused frame stepping
    session.set_paused(false);
    REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    CHECK(state->requests == 1);
    CHECK_FALSE(session.live_microphone().recording());
    CHECK(session.live_microphone().status().contains("prompt"));
    CHECK(session.emulator().microphone_active()); // bound input still works
    for (int frame = 0; frame < 3; ++frame)
        REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    CHECK(state->requests == 1);
    state->access = MicrophoneAccess::Authorized;
    REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    CHECK_FALSE(session.live_microphone().recording()); // grant alone never starts capture
    session.retry_live_microphone();
    REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    CHECK(session.live_microphone().recording());
    state->access = MicrophoneAccess::Denied;
    REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    CHECK_FALSE(session.live_microphone().recording());
    CHECK(session.emulator().microphone_active());
    session.retry_live_microphone();
    REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    CHECK(session.live_microphone().status().contains("denied"));
    state->access = MicrophoneAccess::Authorized;
    session.retry_live_microphone();
    REQUIRE(session.tick() == app::TickResult::FrameAdvanced);
    CHECK(session.live_microphone().recording());
    session.request_stop();
    CHECK_FALSE(session.live_microphone().recording());
    std::filesystem::remove(rom);
}

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
