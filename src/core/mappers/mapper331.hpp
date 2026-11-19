#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 331: BMC-12-IN-1 / 7-in-1 (NS03) multicart.
class Mapper331 final : public Mapper {
  public:
    Mapper331(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool observes_ppu_address() const override;
    void observe_ppu_address(Address addr) override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    [[nodiscard]] Byte active_inner_bank() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    Byte inner_bank_a_ = 0U;
    Byte inner_bank_b_ = 0U;
    Byte outer_bank_ = 0U;
    bool ppu_a12_ = false;
};

} // namespace mapperbus::core
