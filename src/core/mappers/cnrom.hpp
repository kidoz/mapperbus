#pragma once

#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 3: CNROM
/// PRG: 16-32 KB fixed
/// CHR: up to 32 KB ROM, switchable 8 KB banks
class Cnrom : public Mapper {
  public:
    Cnrom(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;

  private:
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    MirrorMode mirror_mode_;
    uint8_t bank_select_ = 0;
    uint8_t num_chr_banks_ = 0; // Number of 8 KB CHR banks
};

} // namespace mapperbus::core
