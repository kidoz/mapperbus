#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 266: UNL-CITYFIGHT, a rewired VRC4 clone with coarse PRG and a 4-bit DAC.
class CityFighter266 final : public Mapper {
  public:
    CityFighter266(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;

    void clock_cpu() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;

    bool has_expansion_audio() const override;
    float audio_output() const override;

    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    uint16_t state_tag() const;
    void write_irq_control(Byte value);
    void write_irq_acknowledge();
    void clock_irq();

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<uint16_t, 8> chr_banks_{};
    MirrorMode initial_mirror_;
    MirrorMode mirror_;
    Byte prg_bank_ = 0U;
    Byte dac_ = 0U;
    Byte irq_latch_ = 0U;
    Byte irq_counter_ = 0U;
    uint16_t irq_prescaler_ = 341U;
    Byte irq_control_ = 0U;
    Byte irq_enabled_ = 0U;
    Byte irq_pending_ = 0U;
};

} // namespace mapperbus::core
