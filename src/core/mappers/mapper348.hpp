#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 348: BMC-830118C MMC3-compatible multicarts.
class Mapper348 final : public Mapper {
  public:
    Mapper348(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
    void power_on() override;
    void reset() override;

    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;

    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    [[nodiscard]] Byte prg_bank(Address addr) const;
    [[nodiscard]] unsigned chr_bank(Address addr) const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    MirrorMode mirror_mode_ = MirrorMode::Vertical;
    std::array<Byte, 8> bank_registers_{};
    Byte bank_select_ = 0U;
    Byte outer_bank_ = 0U;
    Byte irq_latch_ = 0U;
    Byte irq_counter_ = 0U;
    bool irq_reload_ = false;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
    bool outer_enabled_ = false;
    bool outer_write_protected_ = false;
};

} // namespace mapperbus::core
