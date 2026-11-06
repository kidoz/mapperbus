#include "app/emulation_session.hpp"

#include <algorithm>
#include <filesystem>

#include "app/rom_source.hpp"
#include "core/logger.hpp"

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
    reconcile_audio_sample_rate();

    initialized_ = true;
    return {};
}

core::Result<void> EmulationSession::load_rom(const std::string& rom_path) {
    return open_rom(rom_path);
}

core::Result<void> EmulationSession::open_rom(const std::string& rom_path,
                                              std::string_view member) {
    if (!initialized_) {
        auto init_result = initialize();
        if (!init_result) {
            return init_result;
        }
    }

    auto result = load_rom_source(emulator_, rom_path, member, fds_bios_path_);
    if (!result) {
        return std::unexpected(result.error());
    }

    emulator_.reset();
    turbo_input_.reset();
    // Go through set_paused() so a previously suspended audio device is
    // resumed; assigning paused_ directly would leave the new ROM silent.
    set_paused(false);
    running_ = true;
    rom_loaded_ = true;
    current_rom_path_ = rom_path;
    current_rom_member_ = std::move(*result);
    disk_save_frames_ = 0;
    disk_save_error_.clear();
    set_state_storage(state_storage_);
    reset_audio_fill_ratio_history();
    return {};
}

core::Result<void> EmulationSession::insert_disk_side(int side) {
    if (!emulator_.is_fds())
        return std::unexpected("No FDS game is loaded");
    if (side < 0 || side >= emulator_.fds().side_count())
        return std::unexpected("Invalid disk side");
    if (auto saved = flush_disk_changes(); !saved)
        return saved;
    emulator_.fds().insert_side(side);
    return {};
}
core::Result<void> EmulationSession::eject_disk() {
    if (!emulator_.is_fds())
        return std::unexpected("No FDS game is loaded");
    if (auto saved = flush_disk_changes(); !saved)
        return saved;
    emulator_.fds().eject();
    return {};
}
core::Result<void> EmulationSession::set_disk_write_protected(bool protected_disk) {
    if (!emulator_.is_fds())
        return std::unexpected("No FDS game is loaded");
    emulator_.fds().set_write_protected(protected_disk);
    return {};
}
core::Result<void> EmulationSession::export_disk(const std::string& path) {
    return emulator_.export_disk(path);
}

core::Result<void> EmulationSession::flush_disk_changes() {
    auto result = emulator_.flush_disk_changes();
    disk_save_error_ = result ? std::string{} : result.error();
    return result;
}

void EmulationSession::close_rom() {
    if (!rom_loaded_) {
        return;
    }

    if (auto saved = flush_disk_changes(); !saved) {
        core::logger::error("{}", saved.error());
        return;
    }
    emulator_.unload_cartridge();
    turbo_input_.reset();
    rom_loaded_ = false;
    running_ = false;
    // Resume a suspended device as well as clearing the session flag.
    set_paused(false);
    current_rom_path_.clear();
    current_rom_member_.clear();
    state_directory_.clear();
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
    if (++disk_save_frames_ >= 120) {
        disk_save_frames_ = 0;
        if (!emulator_.fds().disk_active()) {
            auto saved = flush_disk_changes();
            if (!saved)
                core::logger::error("{}", saved.error());
        }
    }
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
    turbo_input_.reset();
}

void EmulationSession::power_cycle() {
    if (!rom_loaded_) {
        return;
    }
    emulator_.reset();
    turbo_input_.reset();
    set_paused(false);
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

void EmulationSession::set_state_storage(const StateStorageSettings& settings) {
    state_storage_ = settings;
    if (rom_loaded_) {
        state_directory_ =
            state_directory_for_game(state_storage_, current_rom_path_, game_identity());
    }
}

std::string EmulationSession::game_identity() const {
    return rom_loaded_ ? core::sha256_hex(emulator_.content_hash()) : std::string{};
}

std::string EmulationSession::state_path_for_slot(int slot) const {
    return (state_directory_ / ("slot-" + std::to_string(slot) + ".state")).string();
}

core::Result<std::string> EmulationSession::state_load_path_for_slot(int slot) const {
    if (!rom_loaded_ || slot < 0 || slot >= kSaveStateSlotCount) {
        return std::unexpected(std::string("Load a ROM and choose a slot between 1 and 10"));
    }
    auto primary = state_path_for_slot(slot);
    std::error_code error;
    if (std::filesystem::exists(primary, error)) {
        return primary;
    }
    if (error) {
        return std::unexpected("Cannot access save-state file: " + primary + ": " +
                               error.message());
    }
    if (!current_rom_member_.empty() || emulator_.is_fds()) {
        return primary;
    }
    auto legacy = legacy_state_path(current_rom_path_, slot).string();
    if (std::filesystem::exists(legacy, error)) {
        return legacy;
    }
    if (error) {
        return std::unexpected("Cannot access legacy save-state file: " + legacy + ": " +
                               error.message());
    }
    return primary;
}

core::Result<void> EmulationSession::save_state(int slot) {
    if (slot < 0 || slot >= kSaveStateSlotCount) {
        return std::unexpected(std::string("State slot must be between 1 and 10"));
    }
    if (!rom_loaded_) {
        return std::unexpected(std::string("No ROM loaded to save state from"));
    }
    if (auto directory = ensure_state_directory(state_directory_); !directory) {
        return directory;
    }
    if (auto saved = flush_disk_changes(); !saved)
        return saved;
    return emulator_.save_state_to_file(state_path_for_slot(slot));
}

core::Result<void> EmulationSession::load_state(int slot) {
    const auto path = state_load_path_for_slot(slot);
    if (!path) {
        return std::unexpected(path.error());
    }
    const bool legacy = *path == legacy_state_path(current_rom_path_, slot).string();
    if (legacy) {
        core::logger::warn(
            "Importing a legacy sibling state; old versions cannot verify ROM identity: {}", *path);
    }
    auto result = emulator_.load_state_from_file(*path, legacy);
    if (result) {
        turbo_input_.reset();
    }
    return result;
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

    for (int player = 0; player < 2; ++player) {
        // Consume each turbo action even when ordinary A/B is held, so its
        // phase remains independent of the normal binding.
        const bool turbo_a = turbo_input_.next(
            player, 0, input_->is_action_pressed(player, platform::InputAction::TurboA));
        const bool turbo_b = turbo_input_.next(
            player, 1, input_->is_action_pressed(player, platform::InputAction::TurboB));
        for (auto button : kButtons) {
            bool pressed = input_->is_action_pressed(player, platform::input_action(button));
            if (button == Button::A)
                pressed |= turbo_a;
            if (button == Button::B)
                pressed |= turbo_b;
            emulator_.controller().set_button_state(player, button, pressed);
        }
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
    reconcile_audio_sample_rate();

    return {};
}

void EmulationSession::reconcile_audio_sample_rate() {
    const int device_rate = audio_->actual_sample_rate();
    if (device_rate <= 0 || device_rate == audio_settings_.sample_rate) {
        return;
    }
    // The device could not honor the requested rate and is silently resampling.
    // Reconfigure the APU to the real device rate so SDL3's hidden internal
    // resampler is taken out of the path — its variable buffer latency would
    // otherwise destabilize the DRC feedback loop (heard as crackle).
    audio_settings_.sample_rate = device_rate;
    emulator_.apply_audio_settings(audio_settings_);
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
