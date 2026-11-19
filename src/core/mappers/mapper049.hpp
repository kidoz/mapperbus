#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 049: MMC3 multicart with a gated 128 KiB outer-block register.
class Mapper049 final : public Mapper {
  public:
    Mapper049(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
    void reset() override;

    bool irq_pending() const override {
        return irq_pending_;
    }
    void acknowledge_irq() override {
        irq_pending_ = false;
    }
    void clock_irq_counter() override;

    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    MirrorMode initial_mirror_;
    MirrorMode mirror_mode_;

    Byte outer_ = 0U;
    Byte bank_select_ = 0U;
    bool prg_mode_ = false;
    bool chr_inversion_ = false;
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
