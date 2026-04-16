#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

struct Vrc6Pulse {
    uint8_t duty = 0;   // 4-bit duty cycle (0-15)
    uint8_t volume = 0; // 4-bit volume
    bool mode = false;  // Duty cycle mode (true = constant volume)
    bool enabled = false;
    uint16_t timer_period = 0;
    uint16_t timer = 0;
    uint8_t step = 0;

    void clock();
    uint8_t output() const;
};

struct Vrc6Sawtooth {
    uint8_t accumulator_rate = 0; // 6-bit rate
    bool enabled = false;
    uint16_t timer_period = 0;
    uint16_t timer = 0;
    uint8_t accumulator = 0;
    uint8_t step = 0;

    void clock();
    uint8_t output() const;
};

/// Mapper 24/26: Konami VRC6
/// PRG: 256 KB max, 8/16 KB switchable
/// CHR: 256 KB max, 1 KB switchable
/// Audio: 2 pulse + 1 sawtooth
class Vrc6 : public Mapper {
  public:
    Vrc6(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;

    bool irq_pending() const override {
        return irq_pending_;
    }
    void acknowledge_irq() override {
        irq_pending_ = false;
    }

    bool has_expansion_audio() const override {
        return true;
    }
    void clock_audio() override;
    float audio_output() const override;
    void write_expansion(Address addr, Byte value) override;

  private:
    void update_banks();

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> prg_ram_{};
    std::array<Byte, 0x2000> chr_ram_{};
    bool use_chr_ram_ = false;
    bool swap_address_lines_ = false; // Mapper 26 swaps A0/A1

    // PRG banking
    uint8_t prg_bank_16k_ = 0;
    uint8_t prg_bank_8k_ = 0;
    uint8_t num_prg_banks_8k_ = 0;

    // CHR banking
    std::array<uint8_t, 8> chr_banks_{};

    MirrorMode mirror_mode_ = MirrorMode::Vertical;

    // IRQ
    uint8_t irq_latch_ = 0;
    uint8_t irq_counter_ = 0;
    bool irq_enabled_ = false;
    bool irq_enabled_after_ack_ = false;
    bool irq_pending_ = false;
    bool irq_cycle_mode_ = false; // true=cycle mode, false=scanline mode
    int irq_prescaler_ = 0;

    // Audio channels
    Vrc6Pulse pulse1_;
    Vrc6Pulse pulse2_;
    Vrc6Sawtooth sawtooth_;
};

} // namespace mapperbus::core
