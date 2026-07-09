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

    /// Suspend playback so the device buffer does not run dry (and click on
    /// resume) while the emulator is paused. Default no-op for backends
    /// without a real device.
    virtual void pause() {}
    virtual void resume() {}
};

} // namespace mapperbus::platform
