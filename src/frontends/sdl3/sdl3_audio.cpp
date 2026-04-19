#include "frontends/sdl3/sdl3_audio.hpp"

#include <algorithm>
#include <string>

namespace mapperbus::frontend {

Sdl3Audio::~Sdl3Audio() {
    shutdown();
}

bool Sdl3Audio::initialize(int sample_rate, int buffer_size, int channels) {
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        return false;
    }

    channels_ = channels;

    const int requested_frames = std::max(256, buffer_size);
    const std::string requested_frames_text = std::to_string(requested_frames);
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, requested_frames_text.c_str());
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_STREAM_NAME, "MapperBus");

    SDL_AudioSpec spec{};
    spec.freq = sample_rate;
    spec.format = SDL_AUDIO_F32;
    spec.channels = channels;

    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream_) {
        return false;
    }

    SDL_ResumeAudioStreamDevice(stream_);
    return true;
}

void Sdl3Audio::queue_samples(std::span<const float> samples) {
    if (stream_ && !samples.empty()) {
        SDL_PutAudioStreamData(
            stream_, samples.data(), static_cast<int>(samples.size() * sizeof(float)));
    }
}

void Sdl3Audio::shutdown() {
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
}

int Sdl3Audio::queued_samples() const {
    if (!stream_)
        return 0;
    int bytes = SDL_GetAudioStreamQueued(stream_);
    // Return sample frames (not individual channel samples)
    return bytes / static_cast<int>(sizeof(float) * channels_);
}

} // namespace mapperbus::frontend
