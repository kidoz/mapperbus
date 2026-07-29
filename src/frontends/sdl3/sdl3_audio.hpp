#pragma once

#include <SDL3/SDL.h>

#include "platform/audio/audio_backend.hpp"

namespace mapperbus::frontend {

class Sdl3Audio : public platform::AudioBackend {
  public:
    ~Sdl3Audio() override;

    bool initialize(int sample_rate, int buffer_size, int channels) override;
    void queue_samples(std::span<const float> samples) override;
    void shutdown() override;
    int queued_samples() const override;
    [[nodiscard]] int actual_sample_rate() const override;
    void pause() override;
    void resume() override;

  private:
    SDL_AudioStream* stream_ = nullptr;
    int channels_ = 1;
    int requested_rate_ = 0;
    int actual_rate_ = 0;
};

} // namespace mapperbus::frontend
