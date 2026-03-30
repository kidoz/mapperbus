#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "core/apu/audio_settings.hpp"
#include "core/emulator.hpp"
#include "platform/audio/audio_backend.hpp"
#include "platform/input/input_backend.hpp"
#include "platform/video/video_backend.hpp"

namespace mapperbus::app {

class App {
  public:
    App(std::unique_ptr<platform::VideoBackend> video,
        std::unique_ptr<platform::AudioBackend> audio,
        std::unique_ptr<platform::InputBackend> input,
        const core::AudioSettings& audio_settings = {});
    ~App();

    core::Result<void> initialize(const std::string& rom_path);
    void run();

    core::Emulator& emulator() { return emulator_; }

  private:
    void sync_input();

    core::AudioSettings audio_settings_;
    core::Emulator emulator_;
    std::unique_ptr<platform::VideoBackend> video_;
    std::unique_ptr<platform::AudioBackend> audio_;
    std::unique_ptr<platform::InputBackend> input_;
    bool running_ = false;
};

} // namespace mapperbus::app
