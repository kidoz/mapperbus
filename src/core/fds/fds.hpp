#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::core {

class StateWriter;
class StateReader;

/// Famicom Disk System — disk drive + wavetable audio channel.
///
/// Models the RAM-adapter disk-drive register interface ($4020-$4026 write,
/// $4030-$4033 read), the down-counting timer IRQ, and the sequential
/// byte-transfer engine that streams a mounted disk side. Disk images are the
/// common headered/headerless `.fds` layout (65500-byte sides). Note: booting
/// commercial FDS titles additionally requires the FDS BIOS mapped at
/// $E000-$FFFF, which is a separate (non-distributable) asset.
class Fds {
  public:
    static constexpr std::size_t kSideSize = 65500;

    Fds() = default;

    void reset();
    void step(uint32_t cpu_cycles);

    Byte read(Address addr);
    void write(Address addr, Byte value);

    void save_state(StateWriter& writer) const;
    void load_state(StateReader& reader);

    /// Mounts a `.fds` image (optionally prefixed with the 16-byte "FDS\x1a"
    /// fwNES header). Returns false if the payload is not a whole number of
    /// 65500-byte sides. Side 0 is inserted on success.
    [[nodiscard]] bool load_disk(std::span<const Byte> image);
    void insert_side(int side);
    void eject();

    [[nodiscard]] bool is_loaded() const {
        return loaded_;
    }
    [[nodiscard]] int side_count() const {
        return num_sides_;
    }
    [[nodiscard]] int current_side() const {
        return current_side_;
    }

    [[nodiscard]] bool irq_pending() const {
        return irq_line_;
    }

    // Audio
    void clock_audio();
    float audio_output() const;

  private:
    void reset_audio();
    void reset_drive();
    [[nodiscard]] Byte read_drive_register(Address reg);
    void write_drive_register(Address reg, Byte value);
    void step_timer(uint32_t cpu_cycles);
    void step_transfer(uint32_t cpu_cycles);

    bool loaded_ = false;

    // --- Disk image ---
    std::vector<Byte> disk_data_; // all sides concatenated, kSideSize each
    int num_sides_ = 0;
    int current_side_ = -1; // -1 when no disk is inserted
    bool write_protected_ = true;

    // --- Timer IRQ ($4020/$4021/$4022) ---
    uint16_t irq_reload_ = 0;
    uint16_t irq_timer_ = 0;
    bool irq_timer_enabled_ = false;
    bool irq_timer_repeat_ = false;

    // --- Master I/O enable ($4023) ---
    bool disk_io_enabled_ = false;
    bool sound_io_enabled_ = false;

    // --- Drive control ($4025) ---
    bool motor_on_ = false;
    bool transfer_reset_ = false;
    bool read_mode_ = true;
    bool crc_control_ = false;
    bool irq_on_transfer_ = false;

    // --- Transfer engine ---
    uint32_t disk_position_ = 0; // byte offset within the inserted side
    int32_t transfer_delay_ = 0; // CPU cycles until the next byte
    Byte read_data_ = 0;         // latched byte presented at $4031
    Byte write_data_ = 0;        // byte staged via $4024
    bool transfer_active_ = false;

    // --- Status flags ($4030) ---
    bool timer_irq_flag_ = false;
    bool byte_transfer_flag_ = false;
    bool crc_error_ = false;
    bool end_of_disk_ = false;
    bool irq_line_ = false; // combined timer + transfer IRQ line

    // --- Wavetable audio channel ---
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
