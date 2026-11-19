#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 526: UNL-BJ-56 independent PRG/CHR banking and cycle IRQ.
class Mapper526 final : public Mapper {
  public:
    Mapper526(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
        std::array<Byte, 4> prg_banks{0U, 1U, 2U, 3U};
        std::array<Byte, 8> chr_banks{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
        std::uint16_t irq_counter = 0U;
    };

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    State state_;
};

} // namespace mapperbus::core
