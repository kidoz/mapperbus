#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 467: 47-2 outer-banked MMC3/NROM-128 multicart.
class Mapper467 final : public Mapper {
  public:
    Mapper467(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
    void clock_irq_counter() override;

    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        Byte outer = 0U;
        Byte bank_select = 0U;
        std::array<Byte, 8> bank_registers{};
        Byte irq_latch = 0U;
        Byte irq_counter = 0U;
        bool irq_reload = false;
        bool irq_enabled = false;
        bool irq_pending = false;
    };

    [[nodiscard]] unsigned inner_prg_bank(Address addr) const;
    [[nodiscard]] unsigned inner_chr_bank(Address addr) const;
    [[nodiscard]] std::size_t prg_offset(Address addr) const;
    [[nodiscard]] std::size_t chr_offset(Address addr) const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    Registers registers_{};
};

} // namespace mapperbus::core
