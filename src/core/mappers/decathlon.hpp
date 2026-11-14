#pragma once

#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {
/// Mapper 244: C&E Decathlon's scrambled PRG/CHR latches and bus conflicts.
class Decathlon final : public Mapper {
  public:
    Decathlon(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void power_on() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    uint16_t state_tag() const;
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    MirrorMode mirror_;
    Byte prg_bank_ = 0U;
    Byte chr_bank_ = 0U;
};
} // namespace mapperbus::core
