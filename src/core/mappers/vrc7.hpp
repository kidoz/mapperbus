#pragma once

#include <array>
#include <cmath>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 85: Konami VRC7
/// PRG: 512 KB, 8 KB switchable
/// CHR: 256 KB, 1 KB switchable
/// Audio: 6 FM channels (YM2413 subset with 15 preset instruments)
class Vrc7 : public Mapper {
  public:
    Vrc7(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
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
    void clock_irq_counter() override;

    bool has_expansion_audio() const override {
        return true;
    }
    void clock_audio() override;
    float audio_output() const override;

  private:
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> prg_ram_{};
    std::array<Byte, 0x2000> chr_ram_{};
    bool use_chr_ram_ = false;
    uint8_t num_prg_8k_ = 0;

    std::array<uint8_t, 3> prg_banks_{};
    std::array<uint8_t, 8> chr_banks_{};
    MirrorMode mirror_mode_ = MirrorMode::Vertical;

    // IRQ
    uint8_t irq_latch_ = 0;
    uint8_t irq_counter_ = 0;
    bool irq_enabled_ = false;
    bool irq_enabled_after_ack_ = false;
    bool irq_pending_ = false;
    int irq_prescaler_ = 0;

    // FM audio
    uint8_t audio_register_ = 0;

    struct FmChannel {
        uint16_t frequency = 0; // 9-bit F-number
        uint8_t octave = 0;     // 3-bit octave
        uint8_t instrument = 0; // 4-bit preset
        uint8_t volume = 0;     // 4-bit volume
        bool key_on = false;
        bool sustain = false;
        uint32_t phase = 0; // Phase accumulator
    };

    std::array<FmChannel, 6> fm_channels_{};
    uint16_t audio_divider_ = 0;
};

} // namespace mapperbus::core
