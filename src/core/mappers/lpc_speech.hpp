#pragma once

#include <array>
#include <cstdint>

#include "core/types.hpp"

namespace mapperbus::core {
class StateWriter;
class StateReader;

/// Mapper 241's nibble-stream LPC-PE device. Functional frame/sample model;
/// not the TMS5220 parallel command interface or its microsequencer.
class LpcSpeech {
  public:
    void set_region(Region region);
    void reset();
    void write_nibble(Byte data);
    bool ready() const;
    void clock();
    int16_t output() const {
        return output_;
    }
    void save_state(StateWriter& writer) const;
    void load_state(StateReader& reader);

  private:
    struct Frame {
        int16_t energy = 0;
        int16_t pitch = 0;
        std::array<int16_t, 10> k{};
    };
    unsigned read_bits(unsigned width);
    void parse();
    void start_frame();
    void sample();
    static int16_t wrap(int32_t value);
    static void save_frame(StateWriter& writer, const Frame& frame);
    static void load_frame(StateReader& reader, Frame& frame);
    static bool valid_frame(const Frame& frame);

    std::array<Byte, 16> fifo_{};
    Byte head_ = 0;
    Byte count_ = 0;
    uint32_t cpu_clock_ = 1789773U;
    uint32_t phase_ = 0;
    uint16_t reset_clocks_ = 16000U;
    Byte divider_ = 0;
    Byte frame_sample_ = 0;
    Byte field_ = 0;
    bool repeat_ = false;
    bool playing_ = false;
    bool interpolate_ = false;
    Byte pitch_count_ = 0;
    uint16_t noise_ = 0x1FFFU;
    int16_t output_ = 0;
    Frame current_, target_, pending_;
    std::array<int16_t, 10> delay_{};
};
} // namespace mapperbus::core
