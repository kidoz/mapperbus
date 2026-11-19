#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// GA23C multicart: MMC3 banking/IRQs with four sequential outer-bank registers.
class Mapper045 final : public Mapper {
  public:
    Mapper045(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    std::span<Byte> trainer_ram() override;
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    Result<void> configure_board_jumpers(Byte value) override;
    Result<void> configure_board_profile(std::string_view profile) override;
    bool observes_ppu_address() const override;
    void observe_ppu_address(Address addr) override;
    void power_on() override;
    void reset() override;

    bool irq_pending() const override {
        return irq_pending_;
    }
    void acknowledge_irq() override {
        irq_pending_ = false;
    }
    void clock_irq_counter() override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    Byte raw_chr_bank(Address addr) const;
    unsigned mapped_chr_bank(Address addr) const;
    unsigned mapped_prg_bank(Address addr) const;
    bool prg_rom_disabled(Address addr) const;
    void reset_outer_registers();
    uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 8192> wram_{};
    std::size_t wram_size_ = 0U;
    bool persistent_wram_ = false;
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    MirrorMode mirror_mode_ = MirrorMode::Vertical;

    Byte menu_dip_ = 0U;
    Byte menu_selection_ = 0U;
    Address observed_ppu_address_ = 0U;

    std::array<Byte, 4> outer_registers_{};
    Byte outer_index_ = 0U;

    Byte bank_select_ = 0U;
    bool prg_mode_ = false;
    bool chr_inversion_ = false;
    std::array<Byte, 8> bank_registers_{};

    Byte irq_counter_ = 0U;
    Byte irq_reload_ = 0U;
    bool irq_reload_flag_ = false;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
    bool wram_enabled_ = false;
    bool wram_write_protected_ = false;
};

} // namespace mapperbus::core
