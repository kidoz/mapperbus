#pragma once

#include <string>

#include "app/emulation_session.hpp"

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

    core::Emulator& emulator() {
        return session_.emulator();
    }
    const core::Emulator& emulator() const {
        return session_.emulator();
    }
    EmulationSession& session() {
        return session_;
    }
    const EmulationSession& session() const {
        return session_;
    }

  private:
    EmulationSession session_;
};

} // namespace mapperbus::app
