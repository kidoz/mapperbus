#include "app/emulation_session.hpp"

#include <algorithm>
#include <filesystem>

namespace mapperbus::app {
namespace {
constexpr std::size_t kAudioStagingSamples = 8192;
} // namespace

EmulationSession::EmulationSession(std::unique_ptr<platform::VideoBackend> video,
                                   std::unique_ptr<platform::AudioBackend> audio,
                                   std::unique_ptr<platform::InputBackend> input,
                                   const core::AudioSettings& audio_settings)
    : audio_settings_(audio_settings), emulator_(audio_settings), video_(std::move(video)),
      audio_(std::move(audio)), input_(std::move(input)), audio_staging_(kAudioStagingSamples) {}

EmulationSession::~EmulationSession() {
    if (audio_) {
        audio_->shutdown();
    }
    if (video_) {
        video_->shutdown();
    }
}

core::Result<void> EmulationSession::initialize() {
    if (initialized_) {
        return {};
    }

    if (!video_ || !audio_ || !input_) {
        return std::unexpected(
            std::string("EmulationSession requires video, audio, and input backends"));
    }

    if (!video_->initialize(core::kScreenWidth, core::kScreenHeight)) {
        return std::unexpected(std::string("Failed to initialize video backend"));
    }

    const int channels = audio_settings_.stereo_mode == core::StereoMode::PseudoStereo ? 2 : 1;
    if (!audio_->initialize(
            audio_settings_.sample_rate, audio_settings_.buffer_size_samples, channels)) {
        return std::unexpected(std::string("Failed to initialize audio backend"));
    }

    initialized_ = true;
    return {};
}

core::Result<void> EmulationSession::load_rom(const std::string& rom_path) {
    return open_rom(rom_path);
}

core::Result<void> EmulationSession::open_rom(const std::string& rom_path) {
    if (!initialized_) {
        auto init_result = initialize();
        if (!init_result) {
            return init_result;
        }
    }

    auto result = emulator_.load_cartridge(rom_path);
    if (!result) {
        return result;
    }

    emulator_.reset();
    paused_ = false;
    running_ = true;
    rom_loaded_ = true;
    current_rom_path_ = rom_path;
    reset_audio_fill_ratio_history();
    return {};
}

void EmulationSession::close_rom() {
    if (!rom_loaded_) {
        return;
    }

    emulator_.unload_cartridge();
    rom_loaded_ = false;
    running_ = false;
    paused_ = false;
    current_rom_path_.clear();
    reset_audio_fill_ratio_history();
}

TickResult EmulationSession::tick() {
    if (!running_) {
        return TickResult::Stopped;
    }

    input_->poll();
    if (input_->should_quit()) {
        running_ = false;
        return TickResult::Stopped;
    }

    if (!rom_loaded_) {
        return TickResult::NoCartridge;
    }

    if (paused_) {
        return TickResult::Paused;
    }

    if (audio_->queued_samples() > max_queued_samples()) {
        return TickResult::AudioBackpressure;
    }

    sync_input();
    emulator_.step_frame();
    submit_current_frame();
    return TickResult::FrameAdvanced;
}

TickResult EmulationSession::step_frame() {
    if (!running_) {
        return TickResult::Stopped;
    }

    if (!rom_loaded_) {
        return TickResult::NoCartridge;
    }

    input_->poll();
    if (input_->should_quit()) {
        running_ = false;
        return TickResult::Stopped;
    }

    sync_input();
    emulator_.step_frame();
    submit_current_frame();
    return TickResult::FrameAdvanced;
}

void EmulationSession::reset() {
    emulator_.reset();
}

void EmulationSession::power_cycle() {
    if (!rom_loaded_) {
        return;
    }
    emulator_.reset();
    paused_ = false;
}

void EmulationSession::request_stop() {
    running_ = false;
}

void EmulationSession::set_paused(bool paused) {
    if (paused == paused_) {
        return;
    }
    paused_ = paused;
    // Suspend/resume the device so its buffer does not run dry during the
    // pause (a dry device underruns into hard silence and then snaps to
    // full amplitude on resume — an audible pop).
    if (paused) {
        audio_->pause();
    } else {
        audio_->resume();
    }
}

void EmulationSession::set_region(core::Region region) {
    emulator_.set_region(region);
}

int EmulationSession::audio_queued_samples() const {
    return audio_ ? audio_->queued_samples() : 0;
}

int EmulationSession::audio_low_watermark_samples() const {
    return audio_settings_.sample_rate / 40;
}

int EmulationSession::audio_high_watermark_samples() const {
    return max_queued_samples();
}

core::Result<void> EmulationSession::apply_audio_settings(const core::AudioSettings& settings) {
    const auto previous_settings = audio_settings_;
    audio_settings_ = settings;
    emulator_.apply_audio_settings(audio_settings_);

    if (!initialized_) {
        return {};
    }

    auto result = reinitialize_audio_backend();
    if (result) {
        reset_audio_fill_ratio_history();
        return {};
    }

    const std::string& original_error = result.error();
    audio_settings_ = previous_settings;
    emulator_.apply_audio_settings(audio_settings_);
    auto restore_result = reinitialize_audio_backend();
    if (!restore_result) {
        running_ = false;
        return std::unexpected(original_error + "; failed to restore previous audio settings: " +
                               restore_result.error());
    }
    return std::unexpected(original_error);
}

std::string EmulationSession::state_path_for_slot(int slot) const {
    std::string extension = ".state";
    if (slot > 0) {
        extension += std::to_string(slot);
    }
    return std::filesystem::path(current_rom_path_).replace_extension(extension).string();
}

core::Result<void> EmulationSession::save_state(int slot) {
    if (!rom_loaded_) {
        return std::unexpected(std::string("No ROM loaded to save state from"));
    }
    return emulator_.save_state_to_file(state_path_for_slot(slot));
}

core::Result<void> EmulationSession::load_state(int slot) {
    if (!rom_loaded_) {
        return std::unexpected(std::string("No ROM loaded to load state into"));
    }
    return emulator_.load_state_from_file(state_path_for_slot(slot));
}

core::Result<void> EmulationSession::set_upscaler(std::unique_ptr<platform::Upscaler> upscaler) {
    if (!video_) {
        return std::unexpected(std::string("No video backend available"));
    }

    if (!initialized_) {
        video_->set_upscaler(std::move(upscaler));
        return {};
    }

    video_->shutdown();
    video_->set_upscaler(std::move(upscaler));
    auto result = reinitialize_video_backend();
    if (!result) {
        running_ = false;
        return result;
    }

    if (rom_loaded_) {
        video_->render(emulator_.frame_buffer());
    }

    return {};
}

void EmulationSession::sync_input() {
    using core::Button;
    constexpr Button kButtons[] = {Button::A,
                                   Button::B,
                                   Button::Select,
                                   Button::Start,
                                   Button::Up,
                                   Button::Down,
                                   Button::Left,
                                   Button::Right};

    for (auto button : kButtons) {
        emulator_.controller().set_button_state(0, button, input_->is_button_pressed(0, button));
    }
}

void EmulationSession::submit_current_frame() {
    video_->render(emulator_.frame_buffer());

    const std::size_t count = emulator_.drain_audio(audio_staging_.data(), audio_staging_.size());
    if (count > 0) {
        audio_->queue_samples({audio_staging_.data(), count});
    }

    const float fill_ratio = std::clamp(static_cast<float>(audio_->queued_samples()) /
                                            static_cast<float>(max_queued_samples()),
                                        0.0f,
                                        1.0f);
    emulator_.update_audio_rate_control(smooth_audio_fill_ratio(fill_ratio));
}

float EmulationSession::smooth_audio_fill_ratio(float fill_ratio) {
    audio_fill_ratio_sum_ -= audio_fill_ratio_history_[audio_fill_ratio_index_];
    audio_fill_ratio_history_[audio_fill_ratio_index_] = fill_ratio;
    audio_fill_ratio_sum_ += fill_ratio;

    audio_fill_ratio_index_ = (audio_fill_ratio_index_ + 1) % audio_fill_ratio_history_.size();
    audio_fill_ratio_count_ =
        std::min(audio_fill_ratio_count_ + 1, audio_fill_ratio_history_.size());

    return audio_fill_ratio_sum_ / static_cast<float>(audio_fill_ratio_count_);
}

void EmulationSession::reset_audio_fill_ratio_history() {
    audio_fill_ratio_history_.fill(0.0f);
    audio_fill_ratio_index_ = 0;
    audio_fill_ratio_count_ = 0;
    audio_fill_ratio_sum_ = 0.0f;
}

int EmulationSession::max_queued_samples() const {
    return audio_settings_.sample_rate / 20;
}

core::Result<void> EmulationSession::reinitialize_audio_backend() {
    if (!audio_) {
        return std::unexpected(std::string("No audio backend available"));
    }

    audio_->shutdown();

    const int channels = audio_settings_.stereo_mode == core::StereoMode::PseudoStereo ? 2 : 1;
    if (!audio_->initialize(
            audio_settings_.sample_rate, audio_settings_.buffer_size_samples, channels)) {
        return std::unexpected(std::string("Failed to reinitialize audio backend"));
    }

    return {};
}

core::Result<void> EmulationSession::reinitialize_video_backend() {
    if (!video_) {
        return std::unexpected(std::string("No video backend available"));
    }

    if (!video_->initialize(core::kScreenWidth, core::kScreenHeight)) {
        return std::unexpected(std::string("Failed to reinitialize video backend"));
    }

    return {};
}

} // namespace mapperbus::app
