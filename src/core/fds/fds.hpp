#pragma once

#include <array>
#include <cstdint>

#include "core/types.hpp"

namespace mapperbus::core {

/// Famicom Disk System — includes wavetable audio channel.
class Fds {
  public:
    Fds() = default;

    void reset();
    void step(uint32_t cpu_cycles);

    Byte read(Address addr);
    void write(Address addr, Byte value);

    bool is_loaded() const {
        return loaded_;
    }

    // Audio
    void clock_audio();
    float audio_output() const;

  private:
    bool loaded_ = false;

    // Wavetable audio channel
    std::array<Byte, 64> wavetable_{}; // 64 x 6-bit samples
    bool wave_write_enabled_ = false;
    uint16_t wave_frequency_ = 0;   // 12-bit frequency
    uint32_t wave_accumulator_ = 0; // Phase accumulator
    uint8_t wave_position_ = 0;     // Current sample index (0-63)
    uint8_t volume_envelope_ = 0;   // 6-bit volume (0-63)
    bool volume_enabled_ = true;
    uint8_t master_volume_ = 0; // 2-bit (0-3)
    bool sound_enabled_ = false;
    bool envelope_disabled_ = false;

    // Modulation unit
    std::array<int8_t, 32> mod_table_{}; // Modulation table
    uint16_t mod_frequency_ = 0;         // 12-bit modulation frequency
    uint32_t mod_accumulator_ = 0;
    uint8_t mod_position_ = 0;
    int8_t mod_counter_ = 0;
    bool mod_enabled_ = false;
    uint8_t mod_depth_ = 0; // 6-bit depth
};

} // namespace mapperbus::core
