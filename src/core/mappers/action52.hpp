#pragma once

#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 228: packed three-chip Action 52 and single-chip Cheetahmen II boards.
class Action52 final : public Mapper {
  public:
    Action52(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);
    Byte read_prg(Address addr) override;
    Byte prg_read_mask(Address addr) const override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    uint16_t state_tag() const;
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    bool three_chips_;
    Address address_ = 0U;
    Byte data_ = 0U;
};

} // namespace mapperbus::core
