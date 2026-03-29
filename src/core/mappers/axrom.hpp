#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 7: AxROM (AOROM/AMROM/ANROM)
/// PRG: 128-256 KB, switchable 32 KB banks
/// CHR: 8 KB RAM only
/// Mirroring: single-screen (selected by bit 4 of bank register)
class Axrom : public Mapper {
  public:
    Axrom(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;

  private:
    std::vector<Byte> prg_rom_;
    std::array<Byte, 0x2000> chr_ram_{};
    MirrorMode mirror_mode_;
    uint8_t bank_select_ = 0;
    uint8_t num_banks_ = 0; // Number of 32 KB banks
};

} // namespace mapperbus::core
