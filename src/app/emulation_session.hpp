#pragma once

#include <array>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app/async_disk_save.hpp"
#include "app/live_microphone.hpp"
#include "app/state_storage.hpp"
#include "app/turbo_input.hpp"
#include "core/apu/audio_settings.hpp"
#include "core/emulator.hpp"
#include "platform/audio/audio_backend.hpp"
#include "platform/input/input_backend.hpp"
#include "platform/video/upscaler.hpp"
#include "platform/video/video_backend.hpp"

namespace mapperbus::app {

struct PreparedRom;

enum class TickResult {
    FrameAdvanced,
    AudioBackpressure,
    Paused,
    Stopped,
    NoCartridge,
};

class EmulationSession {
  public:
    EmulationSession(std::unique_ptr<platform::VideoBackend> video,
                     std::unique_ptr<platform::AudioBackend> audio,
                     std::unique_ptr<platform::InputBackend> input,
                     const core::AudioSettings& audio_settings = {},
                     AsyncDiskSave::Write disk_write = AsyncDiskSave::write_snapshot);
    ~EmulationSession();

    core::Result<void> initialize();
    core::Result<void> open_rom(const std::string& rom_path, std::string_view member = {});
    core::Result<void> load_rom(const std::string& rom_path);
    /// Adopt media prepared off-thread after the paused outgoing machine has been saved.
    void adopt_rom(PreparedRom media);
    /// The caller must pause and avoid all machine access while a worker saves this handle.
    [[nodiscard]] std::shared_ptr<core::Emulator> emulator_handle() const {
        return emulator_;
    }
    void close_rom();
    [[nodiscard]] core::Result<void> flush_disk_changes();
    /// GUI opts in; synchronous consumers retain their existing save semantics.
    void enable_background_disk_saves() {
        background_disk_saves_ = true;
    }
    void poll_disk_saves();
    [[nodiscard]] core::Result<void> retry_disk_save();
    [[nodiscard]] bool disk_save_pending() const {
        return disk_saver_.busy();
    }
    [[nodiscard]] PersistenceFence begin_background_persistence() {
        return disk_saver_.begin_handoff();
    }
    void finish_background_persistence(const core::Result<void>& result);
    void cancel_background_persistence() {
        disk_saver_.cancel_handoff();
    }
    [[nodiscard]] const std::string& disk_save_error() const {
        return disk_save_error_;
    }
    void set_fds_bios_path(std::string path) {
        fds_bios_path_ = std::move(path);
    }
    [[nodiscard]] core::Result<void> insert_disk_side(int side);
    [[nodiscard]] core::Result<void> eject_disk();
    [[nodiscard]] core::Result<void> set_disk_write_protected(bool protected_disk);
    [[nodiscard]] core::Result<void> export_disk(const std::string& path);

    TickResult tick();
    TickResult step_frame();

    void reset();
    void power_cycle();
    void request_stop();
    void set_paused(bool paused);
    void set_region(core::Region region);
    void set_controller_timing(core::ControllerTiming timing) {
        emulator_->set_controller_timing(timing);
    }
    void set_expansion_controllers(core::Byte ports) {
        emulator_->set_expansion_controllers(ports);
    }
    void set_controller_mode(core::ControllerMode mode) {
        emulator_->set_controller_mode(mode);
    }
    void set_controller_wiring(core::ControllerWiring wiring) {
        emulator_->set_controller_wiring(wiring);
        if (wiring != core::ControllerWiring::RfFamicom)
            suspend_live_microphone();
    }
    void set_microphone_backend(std::unique_ptr<platform::MicrophoneBackend> backend) {
        clear_microphone_playback();
        live_microphone_.set_backend(std::move(backend));
    }
    void configure_live_microphone(LiveMicrophoneSettings settings);
    void retry_live_microphone() {
        clear_microphone_playback();
        live_microphone_.retry();
    }
    [[nodiscard]] LiveMicrophone& live_microphone() {
        return live_microphone_;
    }
    [[nodiscard]] const LiveMicrophone& live_microphone() const {
        return live_microphone_;
    }
    void set_turbo_period(int frames) {
        turbo_input_.set_period(frames);
    }
    core::Result<void> apply_audio_settings(const core::AudioSettings& settings);
    core::Result<void> set_upscaler(std::unique_ptr<platform::Upscaler> upscaler);

    /// Forwards fullscreen/vsync toggles to the video backend.
    void toggle_fullscreen() {
        if (video_) {
            video_->toggle_fullscreen();
        }
    }
    void set_vsync(bool on) {
        if (video_) {
            video_->set_vsync(on);
        }
    }

    /// Ten content-identified state files (indices 0–9), centralized by default.
    void set_state_storage(const StateStorageSettings& settings);
    /// Installs a location already resolved/validated off-thread; performs no I/O.
    void set_prepared_state_storage(StateStorageSettings settings,
                                    std::filesystem::path directory) {
        state_storage_ = std::move(settings);
        state_directory_ = std::move(directory);
    }
    [[nodiscard]] const std::filesystem::path& state_directory() const {
        return state_directory_;
    }
    [[nodiscard]] std::string game_identity() const;
    core::Result<void> save_state(int slot = 0);
    core::Result<void> load_state(int slot = 0);
    /// Applies already-read bytes transactionally, with no filesystem access.
    core::Result<void> load_state_data(std::span<const core::Byte> data, bool allow_legacy = false);
    [[nodiscard]] std::string state_path_for_slot(int slot = 0) const;
    /// Prefer the configured directory; only a missing file falls back to the legacy sibling.
    [[nodiscard]] core::Result<std::string> state_load_path_for_slot(int slot = 0) const;

    [[nodiscard]] bool initialized() const {
        return initialized_;
    }
    [[nodiscard]] bool running() const {
        return running_;
    }
    [[nodiscard]] bool paused() const {
        return paused_;
    }
    [[nodiscard]] bool has_cartridge() const {
        return rom_loaded_;
    }
    [[nodiscard]] const core::AudioSettings& audio_settings() const {
        return audio_settings_;
    }
    [[nodiscard]] const std::string& current_rom_path() const {
        return current_rom_path_;
    }
    [[nodiscard]] const std::string& current_rom_member() const {
        return current_rom_member_;
    }
    [[nodiscard]] int audio_queued_samples() const;
    [[nodiscard]] int audio_low_watermark_samples() const;
    [[nodiscard]] int audio_high_watermark_samples() const;

    core::Emulator& emulator() {
        return *emulator_;
    }
    const core::Emulator& emulator() const {
        return *emulator_;
    }

  private:
    void sync_input();
    core::Result<void> request_disk_save();
    void submit_current_frame();
    void suspend_live_microphone();
    void clear_microphone_playback();
    [[nodiscard]] float smooth_audio_fill_ratio(float fill_ratio);
    void reset_audio_fill_ratio_history();
    [[nodiscard]] int max_queued_samples() const;
    /// After the audio backend opens, align the APU's sample rate with the
    /// device's actual rate (which may differ from the configured rate when the
    /// platform audio server silently resamples). Updates audio_settings_ and
    /// re-applies settings to the emulator so the DRC watermark/cap and the APU
    /// output rate match the real device.
    void reconcile_audio_sample_rate();
    core::Result<void> reinitialize_audio_backend();
    core::Result<void> reinitialize_video_backend();

    core::AudioSettings audio_settings_;
    std::shared_ptr<core::Emulator> emulator_;
    std::unique_ptr<platform::VideoBackend> video_;
    std::unique_ptr<platform::AudioBackend> audio_;
    std::unique_ptr<platform::InputBackend> input_;
    TurboInput turbo_input_;
    LiveMicrophone live_microphone_;
    bool initialized_ = false;
    bool running_ = false;
    bool paused_ = false;
    bool rom_loaded_ = false;
    std::string current_rom_path_;
    std::string current_rom_member_;
    std::string fds_bios_path_;
    std::string disk_save_error_;
    AsyncDiskSave disk_saver_;
    bool background_disk_saves_ = false;
    int disk_save_frames_ = 0;
    StateStorageSettings state_storage_;
    std::filesystem::path state_directory_;
    std::vector<float> audio_staging_;
    std::array<float, 16> audio_fill_ratio_history_{};
    std::size_t audio_fill_ratio_index_ = 0;
    std::size_t audio_fill_ratio_count_ = 0;
    float audio_fill_ratio_sum_ = 0.0f;
};

} // namespace mapperbus::app
