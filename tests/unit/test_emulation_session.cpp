#include <catch2/catch_test_macros.hpp>
#include <memory>

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
        initialized = true;
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

    bool initialized = false;
    bool shutdown_called = false;
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
    REQUIRE(session.load_rom("BattleCity.nes"));
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

    REQUIRE(session.load_rom("BattleCity.nes"));

    audio_ptr->queued_sample_count = session.emulator().audio_settings().sample_rate;
    REQUIRE(session.tick() == TickResult::AudioBackpressure);

    session.set_paused(true);
    audio_ptr->queued_sample_count = 0;
    REQUIRE(session.tick() == TickResult::Paused);
    REQUIRE(session.step_frame() == TickResult::FrameAdvanced);
}

TEST_CASE("EmulationSession supports close, region change, and audio settings mutation",
          "[app][session]") {
    core::register_builtin_mappers();

    auto audio = std::make_unique<TestAudioBackend>();
    auto* audio_ptr = audio.get();
    EmulationSession session(std::make_unique<platform::NullVideo>(),
                             std::move(audio),
                             std::make_unique<platform::NullInput>());

    REQUIRE(session.open_rom("BattleCity.nes"));
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

    REQUIRE(actions.open_rom("BattleCity.nes"));
    REQUIRE(actions.snapshot().has_cartridge);
    REQUIRE(actions.snapshot().running);
    REQUIRE(actions.snapshot().rom_path == "BattleCity.nes");

    actions.pause();
    REQUIRE(actions.snapshot().paused);

    actions.toggle_pause();
    REQUIRE_FALSE(actions.snapshot().paused);

    actions.set_region(core::Region::PAL);
    REQUIRE(actions.snapshot().region == core::Region::PAL);

    actions.close_rom();
    REQUIRE_FALSE(actions.snapshot().has_cartridge);
    REQUIRE_FALSE(actions.snapshot().running);
    REQUIRE(actions.snapshot().rom_path.empty());
}

} // namespace mapperbus::app
