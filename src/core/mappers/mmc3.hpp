#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 4: MMC3 (TxROM)
/// PRG: up to 512 KB, 8 KB switchable banks
/// CHR: up to 256 KB, 1 KB switchable banks
/// Features: scanline IRQ counter, mirroring control, PRG RAM
class Mmc3 : public Mapper {
  public:
    Mmc3(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;

    bool irq_pending() const override {
        return irq_pending_;
    }
    void acknowledge_irq() override {
        irq_pending_ = false;
    }

    void clock_irq_counter() override;

  private:
    void update_prg_banks();
    void update_chr_banks();

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> prg_ram_{};
    std::array<Byte, 0x2000> chr_ram_{};
    bool use_chr_ram_ = false;

    // Bank select register ($8000)
    uint8_t bank_select_ = 0;    // bits 0-2: register to update
    bool prg_mode_ = false;      // bit 6: PRG bank mode
    bool chr_inversion_ = false; // bit 7: CHR A12 inversion

    // Bank data registers (R0-R7)
    std::array<uint8_t, 8> bank_registers_{};

    // Computed bank offsets (in bytes)
    std::array<uint32_t, 4> prg_offsets_{}; // 4 x 8 KB PRG banks
    std::array<uint32_t, 8> chr_offsets_{}; // 8 x 1 KB CHR banks

    // Mirroring
    MirrorMode mirror_mode_ = MirrorMode::Vertical;

    // IRQ counter
    uint8_t irq_counter_ = 0;
    uint8_t irq_reload_ = 0;
    bool irq_reload_flag_ = false;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
    bool irq_zero_latch_repeats_ = true;

    bool prg_ram_enabled_ = true;
    bool prg_ram_write_protected_ = false;

    uint8_t num_prg_banks_8k_ = 0;
    uint16_t num_chr_banks_1k_ = 0;
};

} // namespace mapperbus::core
