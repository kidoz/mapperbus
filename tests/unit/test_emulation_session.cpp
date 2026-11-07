#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include "../support/test_rom.hpp"
#include "app/emulation_session.hpp"
#include "app/session_actions.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "platform/audio/audio_backend.hpp"
#include "platform/input/null_input.hpp"
#include "platform/video/null_video.hpp"
#include "platform/video/upscaler.hpp"

namespace mapperbus::app {
namespace {

class TestAudioBackend : public platform::AudioBackend {
  public:
    bool initialize(int sample_rate, int buffer_size, int channels) override {
        if (fail_initialize_calls > 0) {
            --fail_initialize_calls;
            return false;
        }
        if (!initialize_result) {
            return false;
        }
        initialized = true;
        initialize_calls += 1;
        last_sample_rate = sample_rate;
        last_buffer_size = buffer_size;
        last_channels = channels;
        return true;
    }

    void queue_samples(std::span<const float> samples) override {
        queued_batches += 1;
        queued_sample_total += samples.size();
    }

    void shutdown() override {
        shutdown_called = true;
    }

    int queued_samples() const override {
        return queued_sample_count;
    }

    void pause() override {
        pause_calls += 1;
        device_paused = true;
    }

    void resume() override {
        resume_calls += 1;
        device_paused = false;
    }

    bool initialized = false;
    bool initialize_result = true;
    bool shutdown_called = false;
    bool device_paused = false;
    int fail_initialize_calls = 0;
    int initialize_calls = 0;
    int pause_calls = 0;
    int resume_calls = 0;
    int last_sample_rate = 0;
    int last_buffer_size = 0;
    int last_channels = 0;
    int queued_sample_count = 0;
    int queued_batches = 0;
    std::size_t queued_sample_total = 0;
};

class TestVideoBackend : public platform::VideoBackend {
  public:
    bool initialize(int width, int height) override {
        initialized = true;
        initialize_calls += 1;
        last_width = width;
        last_height = height;
        return true;
    }

    void render(const core::FrameBuffer& /*frame*/) override {
        render_calls += 1;
    }

    void shutdown() override {
        shutdown_calls += 1;
    }

    void set_upscaler(std::unique_ptr<platform::Upscaler> upscaler) override {
        upscaler_set_calls += 1;
        last_scale_factor = upscaler ? upscaler->scale_factor() : 0;
        upscaler_ = std::move(upscaler);
    }

    bool initialized = false;
    int initialize_calls = 0;
    int shutdown_calls = 0;
    int render_calls = 0;
    int upscaler_set_calls = 0;
    int last_width = 0;
    int last_height = 0;
    int last_scale_factor = 0;
    std::unique_ptr<platform::Upscaler> upscaler_;
};

class TestUpscaler : public platform::Upscaler {
  public:
    explicit TestUpscaler(int factor) : factor_(factor) {}

    int scale_factor() const override {
        return factor_;
    }

    void scale(std::span<const std::uint32_t> /*source*/,
               int /*src_width*/,
               int /*src_height*/,
               std::span<std::uint32_t> /*target*/) override {}

  private:
    int factor_ = 1;
};

[[nodiscard]] std::string test_rom_path() {
    return tests::write_visible_nrom_test_rom("emulation-session").string();
}

} // namespace

TEST_CASE("EmulationSession can initialize and advance frames headlessly", "[app][session]") {
    core::register_builtin_mappers();

    auto audio = std::make_unique<TestAudioBackend>();
    auto* audio_ptr = audio.get();
    EmulationSession session(std::make_unique<platform::NullVideo>(),
                             std::move(audio),
                             std::make_unique<platform::NullInput>());

    REQUIRE(session.initialize());
    REQUIRE(audio_ptr->initialized);
    REQUIRE(session.load_rom(test_rom_path()));
    REQUIRE(session.running());
    REQUIRE(session.has_cartridge());

    const auto outcome = session.tick();
    REQUIRE(outcome == TickResult::FrameAdvanced);
    REQUIRE(audio_ptr->queued_batches > 0);
    REQUIRE(audio_ptr->queued_sample_total > 0);
}

TEST_CASE("EmulationSession reports audio backpressure and paused state", "[app][session]") {
    core::register_builtin_mappers();

    auto audio = std::make_unique<TestAudioBackend>();
    auto* audio_ptr = audio.get();
    EmulationSession session(std::make_unique<platform::NullVideo>(),
                             std::move(audio),
                             std::make_unique<platform::NullInput>());

    REQUIRE(session.load_rom(test_rom_path()));

    audio_ptr->queued_sample_count = session.emulator().audio_settings().sample_rate;
    REQUIRE(session.tick() == TickResult::AudioBackpressure);

    session.set_paused(true);
    audio_ptr->queued_sample_count = 0;
    REQUIRE(session.tick() == TickResult::Paused);
    REQUIRE(session.step_frame() == TickResult::FrameAdvanced);
}

TEST_CASE("EmulationSession suspends and resumes the audio device with pause state",
          "[app][session]") {
    core::register_builtin_mappers();

    auto audio = std::make_unique<TestAudioBackend>();
    auto* audio_ptr = audio.get();
    EmulationSession session(std::make_unique<platform::NullVideo>(),
                             std::move(audio),
                             std::make_unique<platform::NullInput>());

    REQUIRE(session.load_rom(test_rom_path()));

    session.set_paused(true);
    REQUIRE(audio_ptr->pause_calls == 1);
    REQUIRE(audio_ptr->device_paused);

    // Reopening a ROM while paused must resume the suspended device through
    // set_paused(), not just flip the flag, or the new ROM stays silent.
    REQUIRE(session.open_rom(test_rom_path()));
    REQUIRE_FALSE(session.paused());
    REQUIRE(audio_ptr->resume_calls == 1);
    REQUIRE_FALSE(audio_ptr->device_paused);

    // power_cycle() has the same resume contract.
    session.set_paused(true);
    REQUIRE(audio_ptr->device_paused);
    session.power_cycle();
    REQUIRE_FALSE(session.paused());
    REQUIRE(audio_ptr->resume_calls == 2);
    REQUIRE_FALSE(audio_ptr->device_paused);

    // Closing a paused game must keep the logical and device pause states
    // consistent, or the next open never resumes the device and can stall
    // behind its undrained audio queue.
    session.set_paused(true);
    session.close_rom();
    REQUIRE_FALSE(session.has_cartridge());
    CHECK_FALSE(session.paused());
    CHECK_FALSE(audio_ptr->device_paused);
    REQUIRE(session.open_rom(test_rom_path()));
    CHECK_FALSE(audio_ptr->device_paused);
    CHECK(audio_ptr->resume_calls == 3);
    REQUIRE(session.tick() == TickResult::FrameAdvanced);
}

TEST_CASE("EmulationSession supports close, region change, and audio settings mutation",
          "[app][session]") {
    core::register_builtin_mappers();

    auto audio = std::make_unique<TestAudioBackend>();
    auto* audio_ptr = audio.get();
    EmulationSession session(std::make_unique<platform::NullVideo>(),
                             std::move(audio),
                             std::make_unique<platform::NullInput>());

    REQUIRE(session.open_rom(test_rom_path()));
    session.set_region(core::Region::PAL);
    REQUIRE(session.emulator().region() == core::Region::PAL);

    core::AudioSettings settings = session.audio_settings();
    settings.sample_rate = 48000;
    settings.stereo_mode = core::StereoMode::PseudoStereo;
    settings.filter_mode = core::FilterMode::Enhanced;
    settings.dithering_enabled = true;
    REQUIRE(session.apply_audio_settings(settings));
    REQUIRE(session.audio_settings().sample_rate == 48000);
    REQUIRE(session.emulator().audio_settings().sample_rate == 48000);
    REQUIRE(audio_ptr->last_sample_rate == 48000);
    REQUIRE(audio_ptr->last_channels == 2);

    session.close_rom();
    REQUIRE_FALSE(session.has_cartridge());
    REQUIRE_FALSE(session.running());
    REQUIRE(session.current_rom_path().empty());
}

TEST_CASE("EmulationSession keeps current ROM when opening a replacement fails", "[app][session]") {
    core::register_builtin_mappers();

    const std::string rom_path = test_rom_path();
    EmulationSession session(std::make_unique<platform::NullVideo>(),
                             std::make_unique<TestAudioBackend>(),
                             std::make_unique<platform::NullInput>());

    REQUIRE(session.open_rom(rom_path));
    REQUIRE(session.has_cartridge());
    REQUIRE(session.running());

    auto result = session.open_rom("/definitely/not/a/mapperbus/rom.nes");
    REQUIRE_FALSE(result);
    REQUIRE(session.has_cartridge());
    REQUIRE(session.running());
    REQUIRE(session.current_rom_path() == rom_path);
}

TEST_CASE("EmulationSession rolls back audio settings when backend reinit fails",
          "[app][session]") {
    core::register_builtin_mappers();

    auto audio = std::make_unique<TestAudioBackend>();
    auto* audio_ptr = audio.get();
    EmulationSession session(std::make_unique<platform::NullVideo>(),
                             std::move(audio),
                             std::make_unique<platform::NullInput>());

    REQUIRE(session.initialize());
    const auto previous = session.audio_settings();
    audio_ptr->fail_initialize_calls = 1;

    auto next = previous;
    next.sample_rate = 48000;
    auto result = session.apply_audio_settings(next);
    REQUIRE_FALSE(result);
    REQUIRE(session.audio_settings().sample_rate == previous.sample_rate);
    REQUIRE(session.emulator().audio_settings().sample_rate == previous.sample_rate);
}

TEST_CASE("EmulationSession can swap the active upscaler through the video backend",
          "[app][session]") {
    core::register_builtin_mappers();

    auto video = std::make_unique<TestVideoBackend>();
    auto* video_ptr = video.get();
    EmulationSession session(std::move(video),
                             std::make_unique<TestAudioBackend>(),
                             std::make_unique<platform::NullInput>());

    REQUIRE(session.initialize());
    REQUIRE(session.set_upscaler(std::make_unique<TestUpscaler>(4)));
    REQUIRE(video_ptr->shutdown_calls == 1);
    REQUIRE(video_ptr->initialize_calls == 2);
    REQUIRE(video_ptr->upscaler_set_calls == 1);
    REQUIRE(video_ptr->last_scale_factor == 4);
}

TEST_CASE("SessionActions exposes GUI-friendly transport and snapshot state", "[app][actions]") {
    core::register_builtin_mappers();

    EmulationSession session(std::make_unique<platform::NullVideo>(),
                             std::make_unique<TestAudioBackend>(),
                             std::make_unique<platform::NullInput>());
    SessionActions actions(session);

    const std::string rom_path = test_rom_path();
    REQUIRE(actions.open_rom(rom_path));
    REQUIRE(actions.snapshot().has_cartridge);
    REQUIRE(actions.snapshot().running);
    REQUIRE(actions.snapshot().rom_path == rom_path);
    REQUIRE(actions.snapshot().mapper_number == 0);

    actions.pause();
    REQUIRE(actions.snapshot().paused);

    actions.toggle_pause();
    REQUIRE_FALSE(actions.snapshot().paused);

    actions.set_region(core::Region::PAL);
    REQUIRE(actions.snapshot().region == core::Region::PAL);
    actions.set_controller_timing(core::ControllerTiming::RfFamicom);
    actions.set_controller_wiring(core::ControllerWiring::RfFamicom);
    REQUIRE(actions.snapshot().controller_timing == core::ControllerTiming::RfFamicom);
    REQUIRE(actions.snapshot().controller_wiring == core::ControllerWiring::RfFamicom);
    actions.reset();
    actions.power_cycle();
    REQUIRE(actions.snapshot().controller_timing == core::ControllerTiming::RfFamicom);
    REQUIRE(actions.snapshot().controller_wiring == core::ControllerWiring::RfFamicom);

    actions.close_rom();
    REQUIRE_FALSE(actions.snapshot().has_cartridge);
    REQUIRE_FALSE(actions.snapshot().running);
    REQUIRE(actions.snapshot().rom_path.empty());
    REQUIRE_FALSE(actions.snapshot().mapper_number.has_value());
}

static StateStorageSettings temporary_state_storage() {
    StateStorageSettings settings;
    settings.directory =
        std::filesystem::temp_directory_path() /
        ("mapperbus-session-states-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    return settings;
}

TEST_CASE("EmulationSession saves and loads state through centralized slot files",
          "[app][session][state]") {
    core::register_builtin_mappers();

    EmulationSession session(std::make_unique<platform::NullVideo>(),
                             std::make_unique<TestAudioBackend>(),
                             std::make_unique<platform::NullInput>());
    session.set_state_storage(temporary_state_storage());
    SessionActions actions(session);

    SECTION("state operations require a loaded ROM") {
        REQUIRE_FALSE(actions.save_state());
        REQUIRE_FALSE(actions.load_state());
    }

    SECTION("save/load round-trips emulator state") {
        const std::string rom_path = test_rom_path();
        REQUIRE(actions.open_rom(rom_path));

        // UI slot 1 maps to slot-0.state inside the content-identified folder.
        const std::string state_path = actions.state_path();
        REQUIRE(std::filesystem::path(state_path).filename() == "slot-0.state");
        REQUIRE(std::filesystem::path(state_path).parent_path() == session.state_directory());

        for (int i = 0; i < 3; ++i) {
            (void)session.step_frame();
        }
        REQUIRE(actions.save_state());
        REQUIRE(std::filesystem::exists(state_path));
        const std::vector<core::Byte> saved_blob = session.emulator().save_state();

        for (int i = 0; i < 5; ++i) {
            (void)session.step_frame();
        }
        REQUIRE(actions.load_state());
        REQUIRE(session.emulator().save_state() == saved_blob);

        std::filesystem::remove(state_path);
    }

    SECTION("named slots resolve to numbered centralized files") {
        const std::string rom_path = test_rom_path();
        REQUIRE(actions.open_rom(rom_path));
        REQUIRE(std::filesystem::path(actions.state_path(2)).filename() == "slot-2.state");

        REQUIRE(actions.save_state(2));
        REQUIRE(std::filesystem::exists(actions.state_path(2)));
        REQUIRE(actions.load_state(2));

        std::filesystem::remove(actions.state_path(2));
    }
}

TEST_CASE("All ten state slots preserve independent snapshots", "[app][session][state]") {
    core::register_builtin_mappers();
    EmulationSession session(std::make_unique<platform::NullVideo>(),
                             std::make_unique<TestAudioBackend>(),
                             std::make_unique<platform::NullInput>());
    session.set_state_storage(temporary_state_storage());
    SessionActions actions(session);
    const auto rom = tests::write_visible_nrom_test_rom("ten-slots");
    REQUIRE(actions.open_rom(rom.string()));
    std::vector<std::vector<core::Byte>> snapshots;
    for (int slot = 0; slot < 10; ++slot) {
        REQUIRE(session.step_frame() == TickResult::FrameAdvanced);
        snapshots.push_back(session.emulator().save_state());
        REQUIRE(actions.save_state(slot));
    }
    // Unsupported indices must neither overwrite slot 1 nor mutate the machine.
    REQUIRE_FALSE(actions.save_state(-1));
    REQUIRE_FALSE(actions.save_state(10));
    REQUIRE_FALSE(actions.load_state(-1));
    REQUIRE_FALSE(actions.load_state(10));
    REQUIRE(session.emulator().save_state() == snapshots.back());
    for (int slot = 0; slot < 10; ++slot) {
        REQUIRE(actions.load_state(slot));
        REQUIRE(session.emulator().save_state() == snapshots[slot]);
        REQUIRE(std::filesystem::remove(actions.state_path(slot)));
    }
    std::filesystem::remove(rom);
}

TEST_CASE("EmulationSession flushes battery RAM to the .sav file on destruction",
          "[app][session][battery]") {
    core::register_builtin_mappers();

    // INC $6000 in a tight loop: battery-backed PRG-RAM keeps a nonzero
    // counter that must reach the .sav file when the session is destroyed
    // without an explicit close_rom() (the window-quit path).
    constexpr std::uint8_t kProgram[] = {
        0xEE,
        0x00,
        0x60, // INC $6000
        0x4C,
        0x00,
        0x80, // JMP $8000
    };
    const std::filesystem::path rom = tests::build_ines_rom(
        "session-battery", 0, 1, 1, kProgram, core::MirrorMode::Vertical, /*battery=*/true);
    const std::filesystem::path sav = std::filesystem::path(rom).replace_extension(".sav");
    std::filesystem::remove(sav);

    {
        EmulationSession session(std::make_unique<platform::NullVideo>(),
                                 std::make_unique<TestAudioBackend>(),
                                 std::make_unique<platform::NullInput>());
        REQUIRE(session.load_rom(rom.string()));
        for (int i = 0; i < 3; ++i) {
            REQUIRE(session.step_frame() == TickResult::FrameAdvanced);
        }
        // Nothing persists the RAM mid-session; only teardown may write it.
        REQUIRE_FALSE(std::filesystem::exists(sav));
    }

    REQUIRE(std::filesystem::exists(sav));
    REQUIRE(std::filesystem::file_size(sav) == 0x2000);

    std::ifstream saved(sav, std::ios::binary);
    char counter = 0;
    saved.get(counter);
    REQUIRE(counter != 0);

    std::filesystem::remove(sav);
}

TEST_CASE("SessionActions exposes audio queue watermarks", "[app][actions]") {
    auto audio = std::make_unique<TestAudioBackend>();
    auto* audio_ptr = audio.get();

    core::AudioSettings settings;
    settings.sample_rate = 96000;
    EmulationSession session(std::make_unique<platform::NullVideo>(),
                             std::move(audio),
                             std::make_unique<platform::NullInput>(),
                             settings);
    SessionActions actions(session);

    audio_ptr->queued_sample_count = 1234;
    REQUIRE(actions.audio_queued_samples() == 1234);
    REQUIRE(actions.audio_low_watermark_samples() == 2400);
    REQUIRE(actions.audio_high_watermark_samples() == 4800);
}

namespace {
class ActionInput : public platform::InputBackend {
  public:
    void poll() override {
        ++poll_count;
    }
    bool should_quit() const override {
        return false;
    }
    bool is_button_pressed(int player, core::Button button) const override {
        return is_action_pressed(player, platform::input_action(button));
    }
    bool is_action_pressed(int player, platform::InputAction action) const override {
        return held[player][platform::button_index(action)];
    }
    void hold(int player, platform::InputAction action, bool down = true) {
        held[player][platform::button_index(action)] = down;
    }
    std::array<std::array<bool, platform::kInputActionCount>, 4> held{};
    int poll_count = 0;
};

core::Byte read_controller(EmulationSession& session, int player) {
    auto& controller = session.emulator().controller();
    controller.write(1);
    controller.write(0);
    core::Byte state = 0;
    for (int bit = 0; bit < 8; ++bit) {
        state |= (controller.read(player) & 1) << bit;
    }
    return state;
}
} // namespace

TEST_CASE("Turbo generates frame-based NES signals independently for each player",
          "[app][session][turbo]") {
    core::register_builtin_mappers();
    for (const int period : {2, 4, 6}) {
        CAPTURE(period);
        auto input = std::make_unique<ActionInput>();
        auto* held = input.get();
        held->hold(0, platform::InputAction::TurboA);
        held->hold(0, platform::InputAction::Up);
        held->hold(1, platform::InputAction::TurboB);
        EmulationSession session(std::make_unique<platform::NullVideo>(),
                                 std::make_unique<TestAudioBackend>(),
                                 std::move(input));
        session.set_turbo_period(period);
        REQUIRE(session.load_rom(test_rom_path()));
        for (int frame = 0; frame < period * 3; ++frame) {
            CAPTURE(frame);
            REQUIRE(session.tick() == TickResult::FrameAdvanced);
            const bool on = frame % period < period / 2;
            // Repeated strobes must see identical input, never advance turbo.
            for (int read = 0; read < 3; ++read) {
                CHECK(read_controller(session, 0) == (on ? 0x11 : 0x10));
                CHECK(read_controller(session, 1) == (on ? 0x02 : 0x00));
            }
        }
    }
}

TEST_CASE("Both input profiles drive one microphone and pause preserves its carrier",
          "[app][session][microphone]") {
    core::register_builtin_mappers();
    auto input = std::make_unique<ActionInput>();
    auto* held = input.get();
    held->hold(0, platform::InputAction::Microphone);
    EmulationSession session(std::make_unique<platform::NullVideo>(),
                             std::make_unique<TestAudioBackend>(),
                             std::move(input));
    session.set_controller_wiring(core::ControllerWiring::RfFamicom);
    REQUIRE(session.load_rom(test_rom_path()));
    CHECK_FALSE(session.emulator().microphone_active());
    REQUIRE(session.tick() == TickResult::FrameAdvanced);
    CHECK(session.emulator().microphone_active());
    const auto before = session.emulator().save_state();
    session.set_paused(true);
    for (int i = 0; i < 8; ++i) {
        held->poll();
        CHECK(session.tick() == TickResult::Paused);
    }
    CHECK(session.emulator().save_state() == before);
    held->hold(0, platform::InputAction::Microphone, false);
    held->hold(1, platform::InputAction::Microphone);
    REQUIRE(session.step_frame() == TickResult::FrameAdvanced);
    CHECK(session.emulator().microphone_active());
    held->hold(1, platform::InputAction::Microphone, false);
    REQUIRE(session.step_frame() == TickResult::FrameAdvanced);
    CHECK_FALSE(session.emulator().microphone_active());
    held->hold(1, platform::InputAction::Microphone);
    REQUIRE(session.step_frame() == TickResult::FrameAdvanced);
    session.close_rom();
    CHECK_FALSE(session.emulator().microphone_active());
}

TEST_CASE("External host profiles have independent turbo phases and a shared microphone action",
          "[app][session][expansion][turbo]") {
    core::register_builtin_mappers();
    auto input = std::make_unique<ActionInput>();
    auto* held = input.get();
    held->hold(0, platform::InputAction::A);
    held->hold(2, platform::InputAction::TurboA);
    held->hold(3, platform::InputAction::TurboB);
    held->hold(3, platform::InputAction::Microphone);
    EmulationSession session(std::make_unique<platform::NullVideo>(),
                             std::make_unique<TestAudioBackend>(),
                             std::move(input));
    REQUIRE(session.load_rom(test_rom_path()));
    session.set_controller_wiring(core::ControllerWiring::RfFamicom);
    session.set_expansion_controllers(3);
    for (int frame = 0; frame < 8; ++frame) {
        REQUIRE(session.tick() == TickResult::FrameAdvanced);
        auto& external = session.emulator().expansion_controller();
        external.write(1);
        external.write(0);
        CHECK(external.read(0) == (frame % 4 < 2 ? 1 : 0));
        CHECK(external.read(1) == 0);
        CHECK(external.read(1) == (frame % 4 < 2 ? 1 : 0));
        CHECK(read_controller(session, 0) == 1);
        CHECK(read_controller(session, 1) == 0);
        CHECK(session.emulator().microphone_active());
    }
    held->hold(3, platform::InputAction::Microphone, false);
    REQUIRE(session.tick() == TickResult::FrameAdvanced);
    CHECK_FALSE(session.emulator().microphone_active());
}

TEST_CASE("Turbo phase ignores UI polls, pause, and audio backpressure", "[app][session][turbo]") {
    core::register_builtin_mappers();
    auto input = std::make_unique<ActionInput>();
    auto* held = input.get();
    held->hold(0, platform::InputAction::TurboA);
    auto audio = std::make_unique<TestAudioBackend>();
    auto* queue = audio.get();
    EmulationSession session(
        std::make_unique<platform::NullVideo>(), std::move(audio), std::move(input));
    REQUIRE(session.load_rom(test_rom_path()));
    REQUIRE(session.tick() == TickResult::FrameAdvanced); // first on frame
    for (int i = 0; i < 9; ++i)
        held->poll();
    session.set_paused(true);
    for (int i = 0; i < 7; ++i)
        REQUIRE(session.tick() == TickResult::Paused);
    REQUIRE(session.step_frame() == TickResult::FrameAdvanced); // second on frame
    CHECK(read_controller(session, 0) == 1);
    session.set_paused(false);
    queue->queued_sample_count = session.audio_settings().sample_rate;
    for (int i = 0; i < 7; ++i)
        REQUIRE(session.tick() == TickResult::AudioBackpressure);
    queue->queued_sample_count = 0;
    REQUIRE(session.tick() == TickResult::FrameAdvanced); // first off frame
    CHECK(read_controller(session, 0) == 0);
    REQUIRE(session.step_frame() == TickResult::FrameAdvanced); // second off frame
    CHECK(read_controller(session, 0) == 0);
    REQUIRE(session.tick() == TickResult::FrameAdvanced);
    CHECK(read_controller(session, 0) == 1);
}

TEST_CASE("Normal holds, turbo release, and separate A B phases compose correctly",
          "[app][session][turbo]") {
    core::register_builtin_mappers();
    auto input = std::make_unique<ActionInput>();
    auto* held = input.get();
    EmulationSession session(std::make_unique<platform::NullVideo>(),
                             std::make_unique<TestAudioBackend>(),
                             std::move(input));
    REQUIRE(session.load_rom(test_rom_path()));
    held->hold(0, platform::InputAction::TurboA);
    held->hold(0, platform::InputAction::A);
    for (int i = 0; i < 3; ++i) {
        REQUIRE(session.step_frame() == TickResult::FrameAdvanced);
        CHECK(read_controller(session, 0) == 1);
    }
    held->hold(0, platform::InputAction::A, false);
    held->hold(0, platform::InputAction::TurboB);
    REQUIRE(session.step_frame() == TickResult::FrameAdvanced);
    CHECK(read_controller(session, 0) == 2); // A off, newly held B on
    held->hold(0, platform::InputAction::TurboA, false);
    REQUIRE(session.step_frame() == TickResult::FrameAdvanced);
    CHECK(read_controller(session, 0) == 2);
    held->hold(0, platform::InputAction::TurboA);
    REQUIRE(session.step_frame() == TickResult::FrameAdvanced);
    CHECK(read_controller(session, 0) == 1); // A restarts on, B off
    held->hold(0, platform::InputAction::TurboA, false);
    held->hold(0, platform::InputAction::TurboB, false);
    REQUIRE(session.step_frame() == TickResult::FrameAdvanced);
    CHECK(read_controller(session, 0) == 0);
}

TEST_CASE("Turbo restarts on session transitions and speed changes", "[app][session][turbo]") {
    core::register_builtin_mappers();
    auto input = std::make_unique<ActionInput>();
    input->hold(0, platform::InputAction::TurboA);
    EmulationSession session(std::make_unique<platform::NullVideo>(),
                             std::make_unique<TestAudioBackend>(),
                             std::move(input));
    session.set_state_storage(temporary_state_storage());
    REQUIRE(session.load_rom(test_rom_path()));
    REQUIRE(session.save_state(9));
    for (int i = 0; i < 3; ++i)
        REQUIRE(session.step_frame() == TickResult::FrameAdvanced);
    REQUIRE(read_controller(session, 0) == 0);
    SECTION("reset") {
        session.reset();
    }
    SECTION("power cycle") {
        session.power_cycle();
    }
    SECTION("open ROM") {
        REQUIRE(session.open_rom(test_rom_path()));
    }
    SECTION("close and reopen") {
        session.close_rom();
        REQUIRE(session.open_rom(test_rom_path()));
    }
    SECTION("successful load") {
        REQUIRE(session.load_state(9));
    }
    SECTION("speed change") {
        session.set_turbo_period(6);
    }
    SECTION("invalid period falls back to normal") {
        session.set_turbo_period(2);
        session.set_turbo_period(0);
    }
    SECTION("failed load preserves phase") {
        REQUIRE_FALSE(session.load_state(87654));
        REQUIRE(session.step_frame() == TickResult::FrameAdvanced);
        CHECK(read_controller(session, 0) == 0);
    }
    REQUIRE(session.step_frame() == TickResult::FrameAdvanced);
    CHECK(read_controller(session, 0) == 1);
    std::filesystem::remove(session.state_path_for_slot(9));
}

} // namespace mapperbus::app
