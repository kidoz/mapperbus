#pragma once

#include "platform/audio/audio_backend.hpp"

namespace mapperbus::platform {

class NullAudio : public AudioBackend {
  public:
    bool initialize(int /*sample_rate*/, int /*buffer_size*/, int /*channels*/) override {
        return true;
    }
    void queue_samples(std::span<const float> /*samples*/) override {}
    void shutdown() override {}
    int queued_samples() const override {
        return 0;
    }
};

} // namespace mapperbus::platform
