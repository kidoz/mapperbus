#pragma once

#include <SDL3/SDL_audio.h>
#include <memory>

#include "platform/audio/microphone_backend.hpp"
#include "platform/audio/microphone_permission.hpp"

namespace mapperbus::frontend {

class Sdl3Microphone final : public platform::MicrophoneBackend {
  public:
    explicit Sdl3Microphone(std::unique_ptr<platform::MicrophonePermission> permission = {});
    ~Sdl3Microphone() override;
    core::Result<std::vector<std::string>> devices() override;
    core::Result<void> start(std::string_view device) override;
    void stop() override;
    core::Result<std::size_t> read(std::span<float> samples) override;
    core::Result<void> discard() override;

  private:
    core::Result<void> initialize();
    core::Result<void> check_connected();
    core::Result<void> check_permission(bool request_if_needed);
    std::unique_ptr<platform::MicrophonePermission> permission_;
    bool native_permission_ = false;
    SDL_AudioStream* stream_ = nullptr;
    SDL_AudioDeviceID physical_ = 0;
    bool initialized_ = false;
    bool system_default_ = false;
    std::vector<SDL_AudioDeviceID> initial_devices_;
    std::string default_name_;
};

} // namespace mapperbus::frontend
