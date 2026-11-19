#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/konami_vrc.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 183: VRC4e banking/IRQs and an address-latched PRG window at $6000-$7FFF.
class Mapper183 final : public Mapper {
  public:
    Mapper183(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void power_on() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_cpu() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::size_t prg_size_;
    std::size_t chr_size_;
    KonamiVrc vrc_;
    MirrorMode initial_mirror_;
    Byte low_prg_bank_ = 0U;
    Byte irq_latch_ = 0U;
    Byte irq_counter_ = 0U;
    uint16_t irq_prescaler_ = 341U;
    Byte irq_control_ = 0U;
    Byte irq_enabled_ = 0U;
    Byte irq_pending_ = 0U;
};

} // namespace mapperbus::core
