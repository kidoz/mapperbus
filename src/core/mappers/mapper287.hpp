#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 287: BMC-411120-C / BMC-K-3088 MMC3 multicart banking.
class Mapper287 final : public Mapper {
  public:
    Mapper287(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void power_on() override;
    void reset() override;
    Result<void> configure_board_jumpers(Byte value) override;

    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    [[nodiscard]] bool nrom_mode() const;
    [[nodiscard]] uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    MirrorMode initial_mirror_;
    MirrorMode mirror_;
    Byte dip_switch_ = 0U;
    Byte outer_bank_ = 0U;
    Byte bank_select_ = 0U;
    std::array<Byte, 8> bank_registers_{};
    Byte irq_counter_ = 0U;
    Byte irq_latch_ = 0U;
    bool irq_reload_ = false;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
    bool ram_enabled_ = true;
    bool ram_write_protected_ = false;
};

} // namespace mapperbus::core
