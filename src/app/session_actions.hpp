#pragma once

#include <memory>
#include <string>

#include "app/emulation_session.hpp"

namespace mapperbus::app {

struct SessionSnapshot {
    bool initialized = false;
    bool running = false;
    bool paused = false;
    bool has_cartridge = false;
    std::string rom_path;
    core::Region region = core::Region::NTSC;
    core::AudioSettings audio_settings{};
};

class SessionActions {
  public:
    explicit SessionActions(EmulationSession& session);

    core::Result<void> open_rom(const std::string& rom_path);
    void close_rom();
    void pause();
    void resume();
    void toggle_pause();
    void reset();
    void power_cycle();
    void stop();
    void set_region(core::Region region);
    core::Result<void> apply_audio_settings(const core::AudioSettings& settings);
    core::Result<void> set_upscaler(std::unique_ptr<platform::Upscaler> upscaler);

    TickResult tick();
    TickResult step_frame();

    [[nodiscard]] SessionSnapshot snapshot() const;
    [[nodiscard]] EmulationSession& session() {
        return session_;
    }
    [[nodiscard]] const EmulationSession& session() const {
        return session_;
    }

  private:
    EmulationSession& session_;
};

} // namespace mapperbus::app
