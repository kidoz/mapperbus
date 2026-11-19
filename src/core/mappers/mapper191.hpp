#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Xianfeng MMC3 multicart with outer banking and two mirrored CHR-RAM pages.
class Mapper191 final : public Mapper {
  public:
    Mapper191(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void power_on() override;
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
    unsigned effective_outer_bank() const;
    Byte chr_bank(Address addr) const;
    uint16_t state_tag() const;

    const std::vector<Byte> prg_rom_;
    const std::vector<Byte> chr_rom_;
    const bool outer_enabled_;
    const MirrorMode initial_mirror_;

    std::array<Byte, 2048> chr_ram_{};
    Byte outer_bank_ = 0U;
    Byte bank_select_ = 0U;
    bool prg_mode_ = false;
    bool chr_inversion_ = false;
    std::array<Byte, 8> bank_registers_{};
    MirrorMode mirror_mode_;

    Byte irq_counter_ = 0U;
    Byte irq_latch_ = 0U;
    bool irq_reload_ = false;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
};

} // namespace mapperbus::core
