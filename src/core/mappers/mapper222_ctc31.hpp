#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// CTC-31: VRC2-compatible banking with a discrete M2-clocked IRQ circuit.
class Ctc31 final : public Mapper {
  public:
    Ctc31(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void clock_cpu() override;
    bool irq_pending() const override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    uint16_t state_tag() const;
    void increment_irq_counter();
    void set_irq_from_counter();

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    const MirrorMode initial_mirror_;
    std::array<Byte, 2> prg_banks_{};
    std::array<Byte, 8> chr_banks_{};
    MirrorMode mirror_;
    Byte microwire_ = 0;
    Byte irq_latch_ = 0;
    Byte irq_counter_ = 0;
    uint16_t irq_divider_ = 0;
    bool irq_clock_mode_ = false;
    bool irq_pending_ = false;
};

} // namespace mapperbus::core
