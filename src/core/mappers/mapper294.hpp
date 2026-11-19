#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 294: 63-1601 outer banking around sixteen UNROM game blocks.
class Mapper294 final : public Mapper {
  public:
    Mapper294(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    static bool is_outer_register(Address addr);
    void write_outer(Byte value);

    std::vector<Byte> prg_rom_;
    std::array<Byte, 8192> chr_ram_{};
    Byte outer_bank_ = 0U;
    Byte inner_bank_ = 0U;
    MirrorMode mirror_ = MirrorMode::Vertical;
};

} // namespace mapperbus::core
