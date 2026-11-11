#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// MMC3 (4), scrambled 114, Kasheng (115/123), TxSROM (118), TQROM (119), Nitra (250),
/// Sachen 262/512/513, KOF97 (263), T9552 (249 / 4:5), mixed CHR (74/192/194/195),
/// MRCM (196), larger CHR windows (197), TNROM640 (198), and FS309 (199).
/// PRG: 8 KB switchable banks, with mapper-513 outer banks up to 2 MiB
/// CHR: 1 KB switchable banks, up to 512 KiB with mapper-114/115/262 outer pins
/// Features: scanline IRQ counter, mirroring control, PRG RAM
class Mmc3 : public Mapper {
  public:
    Mmc3(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    std::optional<Address> nametable_address(Address addr) const override;
    std::optional<Byte> read_nametable(Address addr) const override;
    bool write_nametable(Address addr, Byte value) override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    Result<void> configure_board_jumpers(Byte value) override;
    Result<void> configure_board_profile(std::string_view profile) override;
    bool observes_ppu_address() const override;
    void observe_ppu_address(Address addr) override;
    void reset() override;

    bool irq_pending() const override {
        return irq_pending_;
    }
    void acknowledge_irq() override {
        irq_pending_ = false;
    }

    void clock_irq_counter() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    void update_prg_banks();
    void update_chr_banks();
    Byte chr_bank(Address addr) const;
    std::optional<std::size_t> mixed_chr_ram_offset(Address addr) const;
    uint16_t mixed_chr_state_tag() const;
    std::optional<std::size_t> fs303_chr_ram_offset(Address addr) const;
    uint16_t fs303_state_tag() const;
    uint32_t t9552_address(uint32_t addr, bool chr) const;
    void save_variant_state(StateWriter& writer) const;
    void load_variant_state(StateReader& reader);

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> prg_ram_{};
    std::array<Byte, 0x2000> chr_ram_{};
    // Mapper 513 has battery-backed pattern memory, not CPU-visible WRAM.
    std::vector<Byte> chr_nvram_;
    Byte ppu_chr_slot_ = 0U;
    // Mapper 262: independent, permanently mapped four-screen VRAM.
    std::vector<Byte> nametable_ram_;
    Byte title_jumper_ = 0U;
    // Immutable mapper-250 PCB wiring selected before cartridge installation.
    bool nitra_prg_mux_ = false;
    // Mapper 262: CHR outer pins/RAM select. Mapper 512: graphics mode.
    Byte board_mode_ = 0U;
    // Mappers 114/115/123: PRG override; 114/115 CHR outer pin; 115 solder pads.
    Byte kasheng_prg_ = 0U;
    Byte kasheng_chr_ = 0U;
    Byte kasheng_solder_pad_ = 0U;
    bool use_chr_ram_ = false;
    // Board wiring and physical memory sizes are immutable cartridge metadata.
    uint16_t mapper_ = 4;
    Byte scramble_profile_ = 0U; // Immutable mapper-114 submapper wiring.
    bool t9552_ = false; // Immutable hardware profile; mapper number selects file bank order.
    Byte t9552_mode_ = 0U;
    // Mappers 74/192/194: immutable CHR RAM population; 194 starts at bank zero.
    std::size_t mixed_chr_ram_size_ = 0U;
    // Mapper 195: PPU-write-selected CHR window and optional $5000-$5FFF RAM.
    Byte fs303_chr_mode_ = 0x80U;
    bool fs303_aux_present_ = false; // Immutable NES 2.0 memory population.
    std::vector<Byte> fs303_aux_ram_;
    Byte mrcm_profile_ = 0U;      // Immutable mapper-196 submapper; legacy uses profile zero.
    Byte mrcm_prg_ = 0U;          // Bit 7: 32 KiB override; low nibble: physical bank.
    Byte mapper197_profile_ = 0U; // Immutable CHR address-pin wiring.
    Byte mapper197_prg_ = 0U;     // Submapper 3: S/P pins controlling PRG A17.
    std::vector<Byte> mapper198_aux_ram_; // Volatile $5000-$5FFF, separate from battery WRAM.
    std::vector<Byte> fs309_nvram_; // Full 32 KiB chip; CPU $5000-$7FFF uses matching offsets.
    std::size_t prg_ram_size_ = 0x2000;
    std::size_t nvram_size_ = 0x2000;
    MirrorMode initial_mirror_ = MirrorMode::Vertical;

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

    std::size_t num_prg_banks_8k_ = 0;
    std::size_t num_chr_banks_1k_ = 0;
};

} // namespace mapperbus::core
