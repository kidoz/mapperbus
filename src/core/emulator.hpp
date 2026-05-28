#pragma once

#include <memory>
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

    [[nodiscard]] Result<void> load_cartridge(const std::string& path);
    void unload_cartridge();
    void reset();
    void step_frame();

    [[nodiscard]] const FrameBuffer& frame_buffer() const {
        return ppu_.frame_buffer();
    }
    [[nodiscard]] const Ppu& ppu() const {
        return ppu_;
    }

    [[nodiscard]] size_t drain_audio(float* dest, size_t max_count) {
        return apu_.drain_samples(dest, max_count);
    }

    void update_audio_rate_control(float buffer_fill_ratio) {
        apu_.update_rate_control(buffer_fill_ratio);
    }

    [[nodiscard]] const AudioSettings& audio_settings() const {
        return apu_.settings();
    }
    void apply_audio_settings(const AudioSettings& settings) {
        apu_.apply_settings(settings);
    }

    [[nodiscard]] Controller& controller() {
        return controller_;
    }
    [[nodiscard]] bool has_cartridge() const {
        return cartridge_ != nullptr;
    }

    [[nodiscard]] Region region() const {
        return region_;
    }

    void set_region(Region region) {
        region_ = region;
        ppu_.set_region(region_);
        apu_.set_region(region_);
    }

  private:
    void wire_apu();
    void clock_expansion_audio(uint32_t cycles);

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
