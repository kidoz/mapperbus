#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 448: 830768C VRC4/UNROM/UOROM/AOROM multicart.
class Mapper448 final : public Mapper {
  public:
    Mapper448(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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

    void power_on() override;
    void reset() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_cpu() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        Byte outer = 0U;
        Byte prg_bank = 0U;
        MirrorMode mirror = MirrorMode::Vertical;
        Byte irq_latch = 0U;
        Byte irq_counter = 0U;
        std::uint16_t irq_prescaler = 341U;
        Byte irq_control = 0U;
        bool irq_enabled = false;
        bool irq_pending = false;
    };

    [[nodiscard]] std::size_t prg_offset(Address addr) const;
    void write_vrc4(Address addr, Byte value);
    void write_irq_control(Byte value);
    void tick_irq();
    [[nodiscard]] std::uint16_t memory_tag() const;

    std::vector<Byte> prg_rom_;
    std::array<Byte, 0x2000> chr_ram_{};
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    Registers registers_{};
};

} // namespace mapperbus::core
