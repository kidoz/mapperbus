#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 524: BTL-900218 VRC2 board with a fixed-period IRQ.
class Mapper524 final : public Mapper {
  public:
    Mapper524(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
        Byte microwire = 0U;
        std::uint16_t irq_counter = 0U;
        Byte irq_enabled = 0U;
    };

    [[nodiscard]] std::uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    State state_;
};

} // namespace mapperbus::core
