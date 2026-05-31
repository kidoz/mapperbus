#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

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

    /// Serializes the full machine state into a self-describing blob
    /// (magic + version + mapper id + region, then each subsystem).
    [[nodiscard]] std::vector<Byte> save_state() const;
    /// Restores state from a blob produced by save_state(). Returns false if
    /// the blob is malformed, the version is unknown, or the mapper id does
    /// not match the currently loaded cartridge (state is left untouched on a
    /// header mismatch).
    [[nodiscard]] bool load_state(std::span<const Byte> data);

    [[nodiscard]] Result<void> save_state_to_file(const std::string& path) const;
    [[nodiscard]] Result<void> load_state_from_file(const std::string& path);

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
