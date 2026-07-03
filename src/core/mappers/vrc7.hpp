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
    void clock_irq_counter() override;

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
    bool audio_halted_ = false; // $E000 bit 6: sound chip held silent/reset

    enum class EnvStage : uint8_t { Idle, Attack, Decay, Sustain, Release };

    /// One FM operator (each channel has a modulator and a carrier).
    /// Trivially copyable: serialized as raw bytes via the FmChannel array.
    struct FmOperator {
        uint32_t phase = 0;   // 18-bit phase accumulator (one period = 1 << 18)
        float env_db = 48.0f; // EG attenuation in dB (0 = loudest, >= 48 = silent)
        EnvStage stage = EnvStage::Idle;
    };

    struct FmChannel {
        uint16_t frequency = 0; // 9-bit F-number
        uint8_t octave = 0;     // 3-bit octave
        uint8_t instrument = 0; // 4-bit patch (0 = user/custom)
        uint8_t volume = 0;     // 4-bit volume
        bool key_on = false;
        bool sustain = false; // $20-$25 bit 5: release at rate 5 on key-off
        FmOperator modulator{};
        FmOperator carrier{};
        float mod_out1 = 0.0f; // two most recent modulator outputs, averaged
        float mod_out2 = 0.0f; // for self-feedback
        float output = 0.0f;   // sample cached by clock_audio() for audio_output()
    };

    std::array<FmChannel, 6> fm_channels_{};
    // User-defined ("custom") instrument patch: the 8 bytes written to
    // registers $00-$07. Channels whose patch number is 0 use this.
    std::array<uint8_t, 8> custom_patch_{};
    uint16_t audio_divider_ = 0;
    // Global LFO phases in periods [0, 1). Not serialized: a few tens of ms
    // of tremolo/vibrato phase after load-state is inaudible.
    float am_lfo_phase_ = 0.0f;
    float fm_lfo_phase_ = 0.0f;

    [[nodiscard]] const uint8_t* patch_for(uint8_t instrument) const;
    void tick_fm_channel(FmChannel& ch, float am_att_db, float vib_mult) const;
    void silence_audio();
};

} // namespace mapperbus::core
