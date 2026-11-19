#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 398: YY840820C VRC4f/GNROM-like multicart.
class Mapper398 final : public Mapper {
  public:
    Mapper398(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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

    bool observes_ppu_address() const override {
        return true;
    }
    void observe_ppu_address(Address addr) override;
    void clock_cpu() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void power_on() override;
    void reset() override;

    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        std::array<Byte, 2> prg_banks{0U, 1U};
        std::array<std::uint16_t, 8> chr_banks{};
        Byte outer = 0xC0U;
        MirrorMode mirror = MirrorMode::Vertical;
        Byte prg_control = 0U;
        Byte irq_latch = 0U;
        Byte irq_counter = 0U;
        std::uint16_t irq_prescaler = 341U;
        Byte irq_control = 0U;
        Byte irq_pending = 0U;
        Byte prg_chr_a12 = 0U;
    };

    [[nodiscard]] std::uint16_t select_chr_bank(Address addr);
    [[nodiscard]] std::size_t chr_offset(Address addr);
    [[nodiscard]] std::uint16_t state_tag() const;
    void write_irq_control(Byte value);
    void acknowledge_irq_register();
    void tick_irq();

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    Registers registers_;
};

} // namespace mapperbus::core
