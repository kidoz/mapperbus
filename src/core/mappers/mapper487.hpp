#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 487: AVE NINA-08 combined NINA-03/Color Dreams multicart.
class Mapper487 final : public Mapper {
  public:
    Mapper487(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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

    void write_expansion(Address addr, Byte value) override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    [[nodiscard]] std::size_t outer_base() const;
    [[nodiscard]] std::size_t prg_offset(Address addr) const;
    [[nodiscard]] std::size_t chr_offset(Address addr) const;
    [[nodiscard]] std::uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    Byte outer_bank_ = 0U;
    Byte nina_inner_bank_ = 0U;
    Byte color_dreams_inner_bank_ = 0U;
};

} // namespace mapperbus::core
