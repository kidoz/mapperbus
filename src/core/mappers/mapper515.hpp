#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 515: Family Noraebang PRG banking, microphone ADC and K-663A audio.
class Mapper515 final : public Mapper {
  public:
    Mapper515(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    void set_microphone_sample(Byte sample);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;

    void power_on() override;
    void reset() override;
    bool has_expansion_audio() const override;
    void clock_audio() override;
    float audio_output() const override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    enum class EnvelopeStage : Byte { Idle, Attack, Decay, Sustain, Release };

    struct FmOperator {
        std::uint32_t phase = 0U;
        float envelope_db = 48.0F;
        EnvelopeStage stage = EnvelopeStage::Idle;
    };

    struct FmVoice {
        FmOperator modulator{};
        FmOperator carrier{};
        float modulator_previous = 0.0F;
        float modulator_current = 0.0F;
        float output = 0.0F;
        bool key_on = false;
    };

    struct AudioState {
        std::array<Byte, 64> registers{};
        std::array<FmVoice, 9> voices{};
        std::uint16_t divider = 0U;
        float amplitude_lfo_phase = 0.0F;
        float vibrato_lfo_phase = 0.0F;
    };

    [[nodiscard]] std::size_t internal_rom_offset() const;
    [[nodiscard]] std::size_t external_rom_size() const;
    [[nodiscard]] Byte read_selected_lower(Address addr) const;
    [[nodiscard]] std::uint16_t state_tag() const;
    void write_audio_register(Byte value);
    void tick_voice(unsigned index, float amplitude_attenuation, float vibrato_multiplier);
    void step_envelope(FmOperator& fm_operator,
                       Byte attack_decay,
                       Byte sustain_release,
                       bool sustained,
                       Byte rate_offset) const;

    std::vector<Byte> prg_rom_;
    std::array<Byte, 0x2000> chr_ram_{};
    MirrorMode initial_mirror_ = MirrorMode::Horizontal;
    Byte prg_bank_ = 0x80U;
    Byte audio_index_ = 0U;
    AudioState audio_{};

    Byte microphone_sample_ = 0U;
    Byte microphone_shift_ = 0U;
    Byte microphone_bits_remaining_ = 0U;
    bool microphone_selected_ = false;
    bool microphone_clock_ = false;
};

} // namespace mapperbus::core
