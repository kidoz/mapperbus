#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 313: four reset-selected games around an MMC3 ASIC.
class Mapper313 final : public Mapper {
  public:
    Mapper313(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
    [[nodiscard]] std::size_t prg_game_size() const;
    [[nodiscard]] std::size_t prg_game_base(Byte game) const;
    [[nodiscard]] Byte chr_bank(Address addr) const;
    [[nodiscard]] std::uint16_t state_tag() const;
    void reset_mmc3();

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    Byte submapper_ = 0U;
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    MirrorMode mirror_ = MirrorMode::Vertical;
    Byte game_ = 0U;
    Byte bank_select_ = 0U;
    std::array<Byte, 8> banks_{};
    Byte irq_counter_ = 0U;
    Byte irq_latch_ = 0U;
    bool irq_reload_ = false;
    bool irq_reload_blocked_ = false;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
};

} // namespace mapperbus::core
