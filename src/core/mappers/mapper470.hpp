#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 470: INX_007T_V01 outer-banked AOROM with CHR RAM.
class Mapper470 final : public Mapper {
  public:
    Mapper470(const INesHeader& header,
              std::vector<Byte> prg_rom,
              const std::vector<Byte>& chr_rom);

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

    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        Byte outer = 0U;
        Byte inner = 0U;
    };

    [[nodiscard]] std::size_t prg_offset(Address addr) const;

    std::vector<Byte> prg_rom_;
    std::array<Byte, 0x2000> chr_ram_{};
    Registers registers_{};
};

} // namespace mapperbus::core
