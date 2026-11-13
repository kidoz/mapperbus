#pragma once

#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 218: one PRG ROM, with console CIRAM also supplying pattern data.
class SingleChip final : public Mapper {
  public:
    SingleChip(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address, Byte) override {}
    // No cartridge pattern memory; the PPU resolves ciram_address instead.
    Byte read_chr(Address) override {
        return 0U;
    }
    void write_chr(Address, Byte) override {}
    std::optional<Address> ciram_address(Address addr) const override;
    MirrorMode mirror_mode() const override;
    void reset() override {}
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    std::vector<Byte> prg_rom_;
    const Byte wiring_;
    const unsigned ciram_pin_;
};

} // namespace mapperbus::core
