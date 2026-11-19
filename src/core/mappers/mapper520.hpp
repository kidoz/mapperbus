#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 520: Datach multicart VRC4e clone with banked CHR-RAM.
class Mapper520 final : public Mapper {
  public:
    Mapper520(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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

    bool observes_ppu_address() const override;
    void observe_ppu_address(Address addr) override;
    void power_on() override;
    void reset() override;
    void clock_cpu() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct State {
        std::array<Byte, 2> prg_banks{0U, 1U};
        std::array<Byte, 8> chr_banks{};
        MirrorMode mirror = MirrorMode::Vertical;
        Byte mode = 0U;
        Byte ppu_chr_slot = 0U;
        Byte irq_latch = 0U;
        Byte irq_counter = 0U;
        std::uint16_t irq_prescaler = 341U;
        Byte irq_control = 0U;
        Byte irq_enabled = 0U;
        Byte irq_pending = 0U;
    };

    [[nodiscard]] unsigned outer_prg_bank() const;
    void irq_control(Byte value);
    void irq_tick();
    [[nodiscard]] std::uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::array<Byte, 0x2000> chr_ram_{};
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    State state_;
};

} // namespace mapperbus::core
