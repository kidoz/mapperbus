#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 528: UNL-831128C two-game FME-7/VRC-IRQ multicart.
class Mapper528 final : public Mapper {
  public:
    Mapper528(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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

    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_cpu() override;

    void reset() override;
    void power_on() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct State {
        Byte game = 0U;
        std::array<Byte, 8> chr_banks{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
        std::array<Byte, 2> prg_banks{0U, 1U};
        Byte low_prg_bank = 0U;
        Byte mirror = 0U;
        Byte irq_latch = 0U;
        Byte irq_counter = 0U;
        std::uint16_t irq_prescaler = 341U;
        Byte irq_control = 0U;
        Byte irq_enabled = 0U;
        Byte irq_pending = 0U;
    };

    std::size_t prg_offset(Byte bank, Address addr) const;
    void tick_irq();

    MirrorMode initial_mirror_;
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> prg_ram_{};
    State state_;
};

} // namespace mapperbus::core
