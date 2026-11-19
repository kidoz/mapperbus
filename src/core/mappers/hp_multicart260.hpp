#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// HP10xx/HP20xx multicarts with MMC3, NROM and CNROM banking modes.
class HpMulticart260 final : public Mapper {
  public:
    HpMulticart260(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    Result<void> configure_board_jumpers(Byte value) override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    unsigned prg_bank(unsigned slot) const;
    unsigned chr_bank(Address addr) const;
    unsigned outer_prg_bank(unsigned inner) const;
    unsigned outer_chr_bank(unsigned inner) const;
    uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 8192> prg_ram_{};
    MirrorMode initial_mirror_;
    MirrorMode mmc3_mirror_;
    bool battery_backed_ = false;
    Byte dip_switches_ = 0U;

    Byte mode_ = 0U;
    Byte prg_base_ = 0U;
    Byte chr_base_ = 0U;
    Byte cnrom_latch_ = 0U;

    Byte bank_select_ = 0U;
    bool prg_mode_ = false;
    bool chr_inversion_ = false;
    std::array<Byte, 8> bank_registers_{};
    bool ram_enabled_ = true;
    bool ram_write_protected_ = false;

    Byte irq_counter_ = 0U;
    Byte irq_reload_ = 0U;
    bool irq_reload_pending_ = false;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
};

} // namespace mapperbus::core
