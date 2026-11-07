#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "app/emulation_session.hpp"

namespace mapperbus::app {

struct SessionSnapshot {
    bool initialized = false;
    bool running = false;
    bool paused = false;
    bool has_cartridge = false;
    bool is_fds = false;
    int disk_side = -1;
    int disk_side_count = 0;
    bool disk_active = false;
    bool disk_write_protected = false;
    bool disk_dirty = false;
    std::string rom_path;
    std::string rom_member;
    std::optional<std::uint16_t> mapper_number;
    core::Region region = core::Region::NTSC;
    core::ControllerTiming controller_timing = core::ControllerTiming::NesAvFamicom;
    core::ControllerWiring controller_wiring = core::ControllerWiring::Nes;
    core::Byte expansion_controllers = 0;
    core::AudioSettings audio_settings{};
};

class SessionActions {
  public:
    explicit SessionActions(EmulationSession& session);

    core::Result<void> open_rom(const std::string& rom_path, std::string_view member = {});
    void close_rom();
    void pause();
    void resume();
    void toggle_pause();
    void reset();
    void power_cycle();
    void stop();
    void set_region(core::Region region);
    void set_controller_timing(core::ControllerTiming timing) {
        session_.set_controller_timing(timing);
    }
    void set_controller_wiring(core::ControllerWiring wiring) {
        session_.set_controller_wiring(wiring);
    }
    void set_expansion_controllers(core::Byte ports) {
        session_.set_expansion_controllers(ports);
    }
    core::Result<void> apply_audio_settings(const core::AudioSettings& settings);
    core::Result<void> set_upscaler(std::unique_ptr<platform::Upscaler> upscaler);
    core::Result<void> save_state(int slot = 0);
    core::Result<void> load_state(int slot = 0);
    [[nodiscard]] std::string state_path(int slot = 0) const;

    TickResult tick();
    TickResult step_frame();

    [[nodiscard]] SessionSnapshot snapshot() const;
    [[nodiscard]] int audio_queued_samples() const;
    [[nodiscard]] int audio_low_watermark_samples() const;
    [[nodiscard]] int audio_high_watermark_samples() const;
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
