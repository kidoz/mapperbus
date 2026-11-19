#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Daou DIS23C01: 16 KiB PRG banking, eight 9-bit CHR banks and volatile WRAM.
class DaouMapper156 final : public Mapper {
  public:
    DaouMapper156(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> prg_ram_{};
    std::array<uint16_t, 8> chr_banks_{};
    Byte prg_bank_ = 0U;
    Byte mirror_ = 2U;
};

} // namespace mapperbus::core
