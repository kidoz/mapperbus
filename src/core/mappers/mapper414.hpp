#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 414: NROM multicart with bus conflicts and solder-pad test mode.
class Mapper414 final : public Mapper {
  public:
    Mapper414(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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

    Result<void> configure_board_profile(std::string_view profile) override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    [[nodiscard]] std::size_t prg_offset(Address addr) const;
    [[nodiscard]] bool prg_rom_disabled(Address addr) const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::uint16_t solder_pads_ = 0U;
    Address control_ = 0U;
    Byte chr_bank_ = 0U;
};

} // namespace mapperbus::core
