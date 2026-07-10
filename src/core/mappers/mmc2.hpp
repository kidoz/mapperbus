#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 9: MMC2 (PxROM) — used by Mike Tyson's Punch-Out!!
///
/// PRG: 8 KB switchable bank at $8000-$9FFF; the remaining 24 KB
/// ($A000-$FFFF) is fixed to the last three 8 KB banks of PRG-ROM.
/// 8 KB PRG-RAM at $6000-$7FFF (battery-backed).
///
/// CHR: two 4 KB banks at $0000-$0FFF and $1000-$1FFF. Each bank has two
/// registered 4 KB pages ("FD" and "FE"). The active page is selected by a
/// hardware latch that toggles when the PPU reads specific pattern-table
/// addresses ($0FD8/$0FE8 for the left bank, $1FD8/$1FE8 for the right).
/// This is how Punch-Out!! swaps background tiles mid-frame.
class Mmc2 : public Mapper {
  public:
    Mmc2(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    void update_chr_banks();

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> prg_ram_{};
    std::array<Byte, 0x2000> chr_ram_{};
    bool use_chr_ram_ = false;

    // PRG bank register ($A000): selects the 8 KB bank at $8000-$9FFF.
    uint8_t prg_bank_ = 0;
    uint8_t num_prg_banks_8k_ = 0;

    // CHR bank registers: two pages per 4 KB half.
    //   chr_banks_[0/1] = FD page for left/right half
    //   chr_banks_[2/3] = FE page for left/right half
    std::array<uint8_t, 4> chr_banks_{};
    // latch_[0] = left half:  0 => FD page active, 1 => FE page active
    // latch_[1] = right half: 0 => FD page active, 1 => FE page active
    std::array<uint8_t, 2> latch_{};
    // Computed 4 KB offsets for the two CHR halves.
    std::array<uint32_t, 2> chr_offsets_{};

    MirrorMode mirror_mode_ = MirrorMode::Horizontal;

    // Power-of-two mask for PRG/CHR-ROM indexing (avoids hardware division
    // on the per-fetch hot path).
    std::size_t prg_rom_mask_ = 0;
    std::size_t chr_rom_mask_ = 0;
};

} // namespace mapperbus::core
