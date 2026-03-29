#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 11: Color Dreams
/// PRG: 32-128 KB, switchable 32 KB banks (bits 0-1 of register)
/// CHR: 32-128 KB ROM, switchable 8 KB banks (bits 4-7 of register)
class ColorDreams : public Mapper {
  public:
    ColorDreams(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;

  private:
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> chr_ram_{};
    MirrorMode mirror_mode_;
    uint8_t prg_bank_ = 0;
    uint8_t chr_bank_ = 0;
    bool use_chr_ram_ = false;
};

} // namespace mapperbus::core
