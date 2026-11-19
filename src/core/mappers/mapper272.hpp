#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 272: VRC2b banking with the NCN-18 PAL's CIRAM and PA13 IRQ overlay.
class Mapper272 final : public Mapper {
  public:
    Mapper272(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool observes_ppu_address() const override {
        return true;
    }
    void observe_ppu_address(Address addr) override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void reset() override;
    void power_on() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    void write_vrc2(Address addr, Byte value);
    uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    const MirrorMode initial_mirror_;
    std::array<Byte, 2> prg_banks_{};
    std::array<Byte, 8> chr_banks_{};
    Byte vrc_mirror_ = 0U;
    Byte pal_mirror_ = 0U;
    Byte irq_counter_ = 0U;
    bool counter_enabled_ = false;
    bool irq_pending_ = false;
    bool previous_a13_ = false;
};

} // namespace mapperbus::core
