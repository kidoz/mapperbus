#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 344: GN-26 MMC3-based multicart with NROM override.
class Mapper344 final : public Mapper {
  public:
    Mapper344(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
    Result<void> configure_board_jumpers(Byte value) override;
    void power_on() override;
    void reset() override;

    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;

    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    [[nodiscard]] Byte chr_bank(Address addr) const;
    [[nodiscard]] bool prg_rom_disabled() const;
    [[nodiscard]] std::uint16_t config_state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> prg_ram_{};
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    MirrorMode mirror_ = MirrorMode::Vertical;
    bool disable_on_solder_test_ = false;

    Byte outer_ = 0U;
    Byte bank_select_ = 0U;
    std::array<Byte, 8> bank_registers_{};
    Byte irq_counter_ = 0U;
    Byte irq_latch_ = 0U;
    bool irq_reload_ = false;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
    bool prg_ram_enabled_ = true;
    bool prg_ram_write_protected_ = false;
};

} // namespace mapperbus::core
