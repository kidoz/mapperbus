#pragma once

#include <memory>
#include <span>
#include <string>

#include "core/apu/apu.hpp"
#include "core/apu/audio_settings.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/cpu/cpu.hpp"
#include "core/fds/fds.hpp"
#include "core/input/controller.hpp"
#include "core/ppu/ppu.hpp"
#include "core/types.hpp"

namespace mapperbus::core {

class Emulator {
  public:
    Emulator();
    explicit Emulator(const AudioSettings& audio_settings);

    Result<void> load_cartridge(const std::string& path);
    void reset();
    void step_frame();

    const FrameBuffer& frame_buffer() const {
        return ppu_.frame_buffer();
    }
    const Ppu& ppu() const {
        return ppu_;
    }

    /// Legacy audio buffer interface (drains ring buffer into staging vector).
    std::span<const float> audio_buffer() const {
        return apu_.output_buffer();
    }
    void clear_audio_buffer() {
        apu_.clear_output_buffer();
    }

    /// Preferred: read available audio samples directly from ring buffer.
    size_t drain_audio(float* dest, size_t max_count) {
        return apu_.drain_samples(dest, max_count);
    }

    void update_audio_rate_control(float buffer_fill_ratio) {
        apu_.update_rate_control(buffer_fill_ratio);
    }

    const AudioSettings& audio_settings() const {
        return apu_.settings();
    }

    Controller& controller() {
        return controller_;
    }

    Region region() const {
        return region_;
    }

  private:
    void wire_apu();

    Region region_ = Region::NTSC;
    MemoryBus bus_;
    Cpu cpu_;
    Ppu ppu_;
    Apu apu_;
    Controller controller_;
    Fds fds_;
    std::unique_ptr<Cartridge> cartridge_;
};

} // namespace mapperbus::core
