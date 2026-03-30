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
    // Calculate maximum audio buffer capacity (50ms worth of frames)
    int max_queued_samples = audio_settings_.sample_rate / 20;

    while (running_) {
        input_->poll();
        if (input_->should_quit()) {
            running_ = false;
            break;
        }

        // Hardware Audio Sync: Block emulator thread if SDL backend has 50ms of audio buffered.
        // This naturally perfectly synchronizes emulation speed (NTSC/PAL/Dendy) to hardware DAC pull requests!
        while (running_ && audio_->queued_samples() > max_queued_samples) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            // Edgecase: OS event pump during deep sleep
            input_->poll();
            if (input_->should_quit()) {
                running_ = false;
                break;
            }
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

        // Apply dynamic rate control
        float fill_ratio = static_cast<float>(audio_->queued_samples()) / static_cast<float>(max_queued_samples);
        emulator_.update_audio_rate_control(fill_ratio);
    }
}

void App::sync_input() {
    using core::Button;
    constexpr Button kButtons[] = {Button::A,
                                  Button::B,
                                  Button::Select,
                                  Button::Start,
                                  Button::Up,
                                  Button::Down,
                                  Button::Left,
                                  Button::Right};

    for (auto btn : kButtons) {
        emulator_.controller().set_button_state(0, btn, input_->is_button_pressed(0, btn));
    }
}

} // namespace mapperbus::app
