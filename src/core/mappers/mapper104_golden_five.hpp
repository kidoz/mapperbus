#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 104: Codemasters Golden Five outer/inner PRG banking and 8 KiB CHR RAM.
class Mapper104GoldenFive final : public Mapper {
  public:
    Mapper104GoldenFive(const INesHeader& header,
                        std::vector<Byte> prg_rom,
                        std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    Byte prg_read_mask(Address addr) const override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    Byte outer_bank() const;

    std::vector<Byte> prg_rom_;
    std::array<Byte, 0x2000> chr_ram_{};
    Byte outer_register_ = 0U;
    Byte inner_register_ = 0U;
};

} // namespace mapperbus::core
