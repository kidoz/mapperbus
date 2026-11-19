#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 320: BMC-830425C-4391T / Super HiK 6-in-1 A-030.
class Mapper320 final : public Mapper {
  public:
    Mapper320(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    [[nodiscard]] std::uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::array<Byte, 8192> chr_ram_{};
    MirrorMode mirror_;
    Byte inner_bank_ = 0U;
    Byte outer_bank_ = 0U;
};

} // namespace mapperbus::core
