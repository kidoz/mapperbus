#pragma once

#include <memory>
#include <string_view>

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

    [[nodiscard]] std::string_view status_text() const;
    [[nodiscard]] bool is_muted() const;
    [[nodiscard]] bool uses_realtime_output() const;
    void set_muted(bool muted);

  private:
    std::unique_ptr<platform::AudioBackend> primary_;
    std::unique_ptr<platform::AudioBackend> fallback_;
    platform::AudioBackend* active_ = nullptr;
    bool using_fallback_ = false;
    bool muted_ = false;
};

} // namespace mapperbus::frontend
