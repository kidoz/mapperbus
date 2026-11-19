#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 404: JY012005 MMC1-based 1998 Super HiK 8-in-1 multicart.
class Mapper404 final : public Mapper {
  public:
    Mapper404(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    void write_prg_cpu(Address addr, Byte value, bool consecutive) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        Byte outer = 0U;
        Byte shift = 0x10U;
        Byte write_count = 0U;
        Byte control = 0x0CU;
        Byte chr_bank0 = 0U;
        Byte chr_bank1 = 0U;
        Byte prg_bank = 0U;
    };

    [[nodiscard]] std::size_t prg_address(Address addr) const;
    [[nodiscard]] std::size_t chr_address(Address addr) const;
    void write_register(Address addr, Byte value);

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    Registers registers_;
};

} // namespace mapperbus::core
