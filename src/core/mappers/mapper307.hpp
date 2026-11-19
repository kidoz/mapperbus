#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Kaiser UNL-KS7037: Metroid FDS conversion using the KS204 register subset.
class Mapper307 final : public Mapper {
  public:
    Mapper307(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    bool maps_prg(Address addr) const override;
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    std::optional<Address> nametable_address(Address addr) const override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    std::vector<Byte> prg_rom_;
    std::array<Byte, 8192> prg_ram_{};
    std::array<Byte, 8192> chr_ram_{};
    Byte bank_select_ = 0U;
    std::array<Byte, 6> banks_{};
};

} // namespace mapperbus::core
