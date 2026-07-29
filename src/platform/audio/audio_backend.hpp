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

    /// The actual output sample rate of the opened device. This may differ
    /// from the rate passed to initialize() when the platform audio server
    /// cannot honor the request and silently resamples (e.g. requesting 96 kHz
    /// from a 48 kHz PipeWire sink). Backends that do not query the device
    /// report the requested rate. The APU should be configured to this rate to
    /// avoid a hidden resampler adding variable latency to the DRC loop.
    [[nodiscard]] virtual int actual_sample_rate() const {
        return -1;
    }

    /// Suspend playback so the device buffer does not run dry (and click on
    /// resume) while the emulator is paused. Default no-op for backends
    /// without a real device.
    virtual void pause() {}
    virtual void resume() {}
};

} // namespace mapperbus::platform
