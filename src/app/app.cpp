#include "app/app.hpp"

#include <array>

namespace mapperbus::app {

App::App(std::unique_ptr<platform::VideoBackend> video,
         std::unique_ptr<platform::AudioBackend> audio,
         std::unique_ptr<platform::InputBackend> input,
         const core::AudioSettings& audio_settings)
    : audio_settings_(audio_settings), emulator_(audio_settings), video_(std::move(video)),
      audio_(std::move(audio)), input_(std::move(input)) {}

App::~App() {
    audio_->shutdown();
    video_->shutdown();
}

core::Result<void> App::initialize(const std::string& rom_path) {
    if (!video_->initialize(core::kScreenWidth, core::kScreenHeight)) {
        return std::unexpected(std::string("Failed to initialize video backend"));
    }

    int channels = audio_settings_.stereo_mode == core::StereoMode::PseudoStereo ? 2 : 1;
    if (!audio_->initialize(
            audio_settings_.sample_rate, audio_settings_.buffer_size_samples, channels)) {
        return std::unexpected(std::string("Failed to initialize audio backend"));
    }

    auto result = emulator_.load_cartridge(rom_path);
    if (!result) {
        return result;
    }

    emulator_.reset();
    running_ = true;
    return {};
}

void App::run() {
    using clock = std::chrono::steady_clock;

    auto timing = core::timing_for_region(emulator_.region());
    auto frame_duration = std::chrono::nanoseconds(timing.frame_duration_ns);
    auto next_frame_time = clock::now();

    int target_buffer_samples = audio_settings_.sample_rate / 10; // ~100ms

    while (running_) {
        input_->poll();
        if (input_->should_quit()) {
            running_ = false;
            break;
        }

        sync_input();
        emulator_.step_frame();
        video_->render(emulator_.frame_buffer());

        // Drain audio from ring buffer and queue to backend
        std::array<float, 8192> audio_staging{};
        size_t n = emulator_.drain_audio(audio_staging.data(), audio_staging.size());
        if (n > 0) {
            audio_->queue_samples({audio_staging.data(), n});
        }

        // Dynamic rate control: keep audio buffer ~50% full
        int queued = audio_->queued_samples();
        float fill_ratio = static_cast<float>(queued) / static_cast<float>(target_buffer_samples);
        emulator_.update_audio_rate_control(fill_ratio);

        // Software timer at exact NTSC rate (~60.0988 Hz)
        next_frame_time += frame_duration;
        auto now = clock::now();
        if (next_frame_time > now) {
            std::this_thread::sleep_until(next_frame_time);
        } else {
            next_frame_time = now;
        }
    }
}

void App::sync_input() {
    using core::Button;
    constexpr Button buttons[] = {Button::A,
                                  Button::B,
                                  Button::Select,
                                  Button::Start,
                                  Button::Up,
                                  Button::Down,
                                  Button::Left,
                                  Button::Right};

    for (auto btn : buttons) {
        emulator_.controller().set_button_state(0, btn, input_->is_button_pressed(0, btn));
    }
}

} // namespace mapperbus::app
