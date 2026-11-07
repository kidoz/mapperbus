#pragma once

#include <array>
#include <memory>
#include <string>

#include "platform/audio/microphone_backend.hpp"

namespace mapperbus::app {

struct LiveMicrophoneSettings {
    bool enabled = false;
    /// Empty selects the system default at capture start; other names match exactly.
    std::string device;
    int sensitivity = 50;
    bool audio_enabled = false;
    int volume_percent = 25;
};

/// Host input policy; raw capture/device state never enters machine snapshots.
class LiveMicrophone {
  public:
    ~LiveMicrophone();
    void set_backend(std::unique_ptr<platform::MicrophoneBackend> backend);
    void configure(LiveMicrophoneSettings settings);
    void suspend();
    void discard();
    void retry();
    [[nodiscard]] bool poll(bool eligible);
    /// Add host PCM to interleaved playback only; never alters activity or core audio.
    void mix_into(std::span<float> samples, int sample_rate, int channels);
    [[nodiscard]] core::Result<std::vector<std::string>> devices();
    [[nodiscard]] std::string status() const;
    [[nodiscard]] const LiveMicrophoneSettings& settings() const {
        return settings_;
    }
    [[nodiscard]] bool recording() const {
        return recording_;
    }

  private:
    void fail(const std::string& error);
    void clear_mix_audio();
    void append_mix_audio(std::span<const float> samples);
    LiveMicrophoneSettings settings_;
    std::unique_ptr<platform::MicrophoneBackend> backend_;
    std::string error_;
    bool recording_ = false;
    bool active_ = false;
    std::array<float, 2048> mix_audio_{};
    std::size_t mix_head_ = 0;
    std::size_t mix_size_ = 0;
    double mix_phase_ = 0;
};

} // namespace mapperbus::app
