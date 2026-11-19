#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 037: Nintendo's three-game MMC3 multicart with outer ROM bank pins.
class Mmc3Multicart37 final : public Mapper {
  public:
    Mmc3Multicart37(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
    void clock_irq_counter() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    uint16_t state_tag() const;
    Byte inner_prg_bank(unsigned slot) const;
    Byte physical_prg_bank(Byte inner_bank) const;
    Byte chr_bank(Address addr) const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    MirrorMode initial_mirror_;
    MirrorMode mirror_mode_;

    Byte outer_bank_ = 0U;
    Byte bank_select_ = 0U;
    std::array<Byte, 8> bank_registers_{};

    Byte irq_counter_ = 0U;
    Byte irq_reload_ = 0U;
    bool irq_reload_flag_ = false;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;

    bool prg_ram_enabled_ = true;
    bool prg_ram_write_protected_ = false;
};

} // namespace mapperbus::core
