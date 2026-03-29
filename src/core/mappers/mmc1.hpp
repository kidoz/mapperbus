#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 1: MMC1 (SxROM)
/// PRG: up to 512 KB, 16 KB or 32 KB switching modes
/// CHR: up to 128 KB, 4 KB or 8 KB switching modes
/// Features: serial shift register (5 writes), mirroring control, PRG RAM
class Mmc1 : public Mapper {
  public:
    Mmc1(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;

  private:
    void write_register(Address addr, Byte value);
    void update_banks();

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> prg_ram_{};
    std::array<Byte, 0x2000> chr_ram_{};
    bool use_chr_ram_ = false;

    // Shift register (serial interface)
    Byte shift_register_ = 0x10; // Bit 4 set = empty sentinel
    uint8_t write_count_ = 0;

    // Internal registers
    Byte control_ = 0x0C; // $8000-$9FFF: mirroring, PRG mode, CHR mode
    Byte chr_bank0_ = 0;  // $A000-$BFFF: CHR bank 0
    Byte chr_bank1_ = 0;  // $C000-$DFFF: CHR bank 1
    Byte prg_bank_ = 0;   // $E000-$FFFF: PRG bank + RAM enable

    // Computed bank offsets
    uint32_t prg_offset0_ = 0; // $8000-$BFFF
    uint32_t prg_offset1_ = 0; // $C000-$FFFF
    uint32_t chr_offset0_ = 0; // $0000-$0FFF (or $0000-$1FFF in 8KB mode)
    uint32_t chr_offset1_ = 0; // $1000-$1FFF (4KB mode only)

    MirrorMode mirror_mode_ = MirrorMode::Horizontal;
    uint8_t num_prg_banks_ = 0; // In 16 KB units
};

} // namespace mapperbus::core
