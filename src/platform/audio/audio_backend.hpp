#pragma once

#include <span>

namespace mapperbus::platform {

class AudioBackend {
  public:
    virtual ~AudioBackend() = default;

    virtual bool initialize(int sample_rate, int buffer_size, int channels = 1) = 0;
    virtual void queue_samples(std::span<const float> samples) = 0;
    virtual void shutdown() = 0;
    virtual int queued_samples() const = 0;
};

} // namespace mapperbus::platform
