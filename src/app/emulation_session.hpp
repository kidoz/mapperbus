#pragma once

#include <memory>
#include <string>

#include "core/apu/audio_settings.hpp"
#include "core/emulator.hpp"
#include "platform/audio/audio_backend.hpp"
#include "platform/input/input_backend.hpp"
#include "platform/video/upscaler.hpp"
#include "platform/video/video_backend.hpp"

namespace mapperbus::app {

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
                     const core::AudioSettings& audio_settings = {});
    ~EmulationSession();

    core::Result<void> initialize();
    core::Result<void> open_rom(const std::string& rom_path);
    core::Result<void> load_rom(const std::string& rom_path);
    void close_rom();

    TickResult tick();
    TickResult step_frame();

    void reset();
    void power_cycle();
    void request_stop();
    void set_paused(bool paused);
    void set_region(core::Region region);
    core::Result<void> apply_audio_settings(const core::AudioSettings& settings);
    core::Result<void> set_upscaler(std::unique_ptr<platform::Upscaler> upscaler);

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
    [[nodiscard]] int audio_queued_samples() const;
    [[nodiscard]] int audio_low_watermark_samples() const;
    [[nodiscard]] int audio_high_watermark_samples() const;

    core::Emulator& emulator() {
        return emulator_;
    }
    const core::Emulator& emulator() const {
        return emulator_;
    }

  private:
    void sync_input();
    void submit_current_frame();
    [[nodiscard]] int max_queued_samples() const;
    core::Result<void> reinitialize_audio_backend();
    core::Result<void> reinitialize_video_backend();

    core::AudioSettings audio_settings_;
    core::Emulator emulator_;
    std::unique_ptr<platform::VideoBackend> video_;
    std::unique_ptr<platform::AudioBackend> audio_;
    std::unique_ptr<platform::InputBackend> input_;
    bool initialized_ = false;
    bool running_ = false;
    bool paused_ = false;
    bool rom_loaded_ = false;
    std::string current_rom_path_;
};

} // namespace mapperbus::app
