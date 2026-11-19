#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 105: NES-EVENT, an MMC1 board with two PRG chips and an M2 timer.
class NesEvent final : public Mapper {
  public:
    NesEvent(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    void write_prg_cpu(Address addr, Byte value, bool consecutive) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    Result<void> configure_board_jumpers(Byte value) override;
    bool irq_pending() const override;
    void clock_cpu() override;
    void reset() override;
    void power_on() override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    void write_register(Address addr, Byte value);
    uint32_t irq_threshold() const;
    uint16_t state_tag() const;
    unsigned prg_page(Address addr) const;

    std::vector<Byte> prg_rom_;
    std::array<Byte, 8192> wram_{};
    std::array<Byte, 8192> chr_ram_{};
    Byte dips_ = 0U;
    Byte shift_register_ = 0x10U;
    Byte write_count_ = 0U;
    Byte control_ = 0U;
    Byte chr_bank0_ = 0U;
    Byte chr_bank1_ = 0U;
    Byte prg_bank_ = 0U;
    bool initialized_ = false;
    bool irq_pending_ = false;
    uint32_t counter_ = 0U;
};

} // namespace mapperbus::core
