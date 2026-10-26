#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "platform/audio/audio_backend.hpp"

namespace mapperbus::frontend {

class GuiAudioBackend : public platform::AudioBackend {
  public:
    GuiAudioBackend();
    ~GuiAudioBackend() override;

    bool initialize(int sample_rate, int buffer_size, int channels) override;
    void queue_samples(std::span<const float> samples) override;
    void shutdown() override;
    int queued_samples() const override;
    void pause() override;
    void resume() override;

    [[nodiscard]] std::string_view status_text() const;
    [[nodiscard]] bool is_muted() const;
    [[nodiscard]] bool uses_realtime_output() const;
    void set_muted(bool muted);
    /// Linear loudness in [0, 1]. Samples are scaled before they reach the device.
    void set_volume(float volume);
    [[nodiscard]] float volume() const;

  private:
    std::unique_ptr<platform::AudioBackend> primary_;
    std::unique_ptr<platform::AudioBackend> fallback_;
    platform::AudioBackend* active_ = nullptr;
    std::vector<float> scaled_samples_;
    bool using_fallback_ = false;
    bool muted_ = false;
    float volume_ = 1.0F;
};

} // namespace mapperbus::frontend
