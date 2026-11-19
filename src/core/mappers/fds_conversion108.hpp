#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 108: four FDS-to-cartridge conversion boards selected by NES 2.0 submapper.
class FdsConversion108 final : public Mapper {
  public:
    FdsConversion108(const INesHeader& header,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> chr_rom);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void power_on() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    uint16_t state_tag() const;
    uint32_t saved_chr_size() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_;
    std::array<Byte, 8192> chr_ram_{};
    MirrorMode mirror_;
    Byte submapper_;
    bool has_chr_ram_;
    Byte prg_bank_ = 0U;
    Byte chr_bank_ = 0U;
};

} // namespace mapperbus::core
