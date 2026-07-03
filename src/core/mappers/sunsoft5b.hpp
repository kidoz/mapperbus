#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 69: Sunsoft FME-7 / 5B
/// PRG: 256 KB, 8 KB switchable
/// CHR: 256 KB, 1 KB switchable
/// Audio (5B variant): 3 square channels (AY-3-8910 subset)
class Sunsoft5b : public Mapper {
  public:
    Sunsoft5b(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> prg_ram_{};
    std::array<Byte, 0x2000> chr_ram_{};
    bool use_chr_ram_ = false;
    uint8_t num_prg_8k_ = 0;

    // Banking
    uint8_t command_ = 0;
    std::array<uint8_t, 8> chr_banks_{};
    std::array<uint8_t, 4> prg_banks_{};
    bool prg_ram_enabled_ = false;
    MirrorMode mirror_mode_ = MirrorMode::Vertical;

    // IRQ
    uint16_t irq_counter_ = 0;
    bool irq_enabled_ = false;
    bool irq_counter_enabled_ = false;
    bool irq_pending_ = false;

    // Audio: 3 AY square channels + noise + envelope (YM2149 subset)
    struct AyChannel {
        uint16_t period = 0;
        uint16_t timer = 0;
        uint8_t volume = 0;
        bool use_envelope = false; // volume reg bit 4: envelope drives level
        bool tone_enabled = true;
        bool noise_enabled = true;
        bool output_state = false;
    };

    void clock_envelope();

    std::array<AyChannel, 3> channels_{};
    uint8_t ay_register_ = 0;
    uint8_t audio_divider_ = 0; // AY runs at CPU/16

    // Noise generator (regs $06/$07)
    uint8_t noise_period_ = 0; // 5-bit
    uint16_t noise_timer_ = 0;
    uint32_t noise_lfsr_ = 1; // 17-bit YM2149 LFSR; must seed nonzero

    // Envelope generator (regs $0B-$0D)
    uint16_t env_period_ = 0;
    uint16_t env_timer_ = 0;
    uint8_t env_shape_ = 0; // $0D bits: 3=continue 2=attack 1=alternate 0=hold
    uint8_t env_step_ = 0;  // position within the current 32-step ramp
    uint8_t env_level_ = 0; // current 5-bit output level
    bool env_attack_ = false;
    bool env_holding_ = false;
};

} // namespace mapperbus::core
