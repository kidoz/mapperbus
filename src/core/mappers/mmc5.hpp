#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

struct Mmc5Pulse {
    uint8_t duty = 0;
    uint8_t sequence_pos = 0;
    uint16_t timer_period = 0;
    uint16_t timer = 0;
    uint8_t length_counter = 0;
    bool enabled = false;
    bool halt_length = false;
    bool constant_volume = false;
    uint8_t volume = 0;
    uint8_t envelope_period = 0;
    uint8_t envelope_divider = 0;
    uint8_t envelope_decay = 0;
    bool envelope_start = false;

    void clock_timer();
    void clock_envelope();
    void clock_length();
    uint8_t output() const;
};

/// Mapper 5: MMC5 (ExROM)
/// PRG: up to 1 MB, flexible banking
/// CHR: up to 1 MB
/// Audio: 2 pulse + PCM
class Mmc5 : public Mapper {
  public:
    Mmc5(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    bool maps_expansion(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void power_on() override;
    void reset() override;

    bool has_expansion_audio() const override {
        return true;
    }
    void clock_audio() override;
    float audio_output() const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    void load_state_version(StateReader& reader, std::uint32_t version) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

    // MMC5 separates sprite and background CHR banks and picks the set by
    // observing which PPU fetch is in flight. The render-context hooks model
    // that distinction directly; CPU/PPUDATA reads use the last-written set.
    bool uses_render_context() const override {
        return !use_chr_ram_;
    }
    std::optional<BackgroundFetch> background_fetch(Address nt,
                                                    Address pattern,
                                                    unsigned column,
                                                    unsigned scanline) const override;
    std::optional<std::array<Byte, 2>> sprite_pattern(unsigned oam,
                                                      Address pattern,
                                                      bool tall) const override;
    std::optional<Address> nametable_address(Address addr) const override;

    // Scanline IRQ ($5203/$5204) and PCM IRQ ($5010); both are level IRQs
    // cleared by reading their respective status register.
    bool irq_pending() const override {
        return irq_pending_ || (pcm_irq_enabled_ && pcm_irq_flag_);
    }
    void acknowledge_irq() override {
        irq_pending_ = false;
    }
    void clock_irq_counter() override;
    void on_ppu_frame_start() override;

  private:
    /// CHR bank for a fetch at `addr` in the sprite or background register
    /// set, in current-window units ($5101 selects the window size).
    [[nodiscard]] uint16_t chr_bank_for(Address addr, bool sprite_set) const;
    /// Current $5101 window size in bytes (0x2000 down to 0x0400).
    [[nodiscard]] uint32_t chr_window_size() const;
    /// Both pattern planes read through one register set; nullopt on CHR RAM
    /// carts (the ordinary read_chr path applies there).
    [[nodiscard]] std::optional<std::array<Byte, 2>> chr_planes(Address pattern,
                                                                bool sprite_set) const;
    /// CIRAM page pair matching the header mirroring, for $5105 defaults and
    /// pre-v23 snapshot reconstruction.
    [[nodiscard]] std::array<uint8_t, 4> selects_from_mirror_mode() const;
    /// Nearest single mirror_mode_ for the current $5105 quadrant selects,
    /// used only as the fallback for the unmodeled ExRAM/fill selections.
    [[nodiscard]] MirrorMode mirroring_fallback() const;

    static constexpr std::uint32_t kMultiplierStateVersion = 22;
    static constexpr std::uint32_t kChrRegisterSetsStateVersion = 23;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x10000> prg_ram_{}; // up to 64 KB battery/work RAM
    std::array<Byte, 0x2000> chr_ram_{};
    bool use_chr_ram_ = false;

    // PRG banking: mode 0 (32K) .. mode 3 (four 8K). $5113-$5117 hold raw
    // register bytes; for $5114-$5117 bit7 selects ROM (1) vs PRG-RAM (0).
    uint8_t prg_mode_ = 3;
    std::array<uint8_t, 5> prg_banks_{};
    std::size_t num_prg_8k_ = 0;
    std::size_t num_prg_ram_8k_ = 8; // 64 KB / 8 KB

    // CHR banking: $5101 mode (0:8K, 1:4K, 2:2K, 3:1K). Two register sets:
    // $5120-$5127 for sprites, $5128-$512B for the background (each covering
    // a mirrored pair of 1 KB windows). Rendered fetches pick a set by fetch
    // type; PPUDATA uses whichever set was written last.
    uint8_t chr_mode_ = 3;
    std::array<uint16_t, 8> chr_banks_1k_{};
    std::array<uint8_t, 4> bg_chr_banks_{};
    bool last_write_sprite_set_ = true;

    // $5105: two bits per nametable quadrant. 0/1 select CIRAM page 0/1;
    // 2 (ExRAM) and 3 (fill) are not modeled and fall back to mirror_mode_.
    std::array<uint8_t, 4> nametable_selects_{};

    MirrorMode mirror_mode_ = MirrorMode::Vertical;
    std::array<Byte, 2> multiplier_operands_{};

    // Scanline IRQ state
    uint8_t irq_target_ = 0;
    uint16_t irq_scanline_ = 0;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
    bool in_frame_ = false;
    // CPU cycles since the last rendered-scanline tick; clears in_frame_ when
    // the ticks stop (vblank start / rendering off). Derived state, not
    // serialized: it rebuilds from the PPU tick stream after a load.
    uint16_t in_frame_watchdog_ = 0;

    // Audio
    Mmc5Pulse pulse1_;
    Mmc5Pulse pulse2_;
    uint8_t pcm_output_ = 0;
    bool pcm_read_mode_ = false;   // $5010 bit 0: PCM fed by $8000-$BFFF reads
    bool pcm_irq_enabled_ = false; // $5010 bit 7
    bool pcm_irq_flag_ = false;    // set by a $00 PCM sample; cleared by $5010 read
    uint32_t audio_cycle_ = 0;

    static constexpr std::array<std::array<uint8_t, 8>, 4> kDuty = {{
        {0, 1, 0, 0, 0, 0, 0, 0},
        {0, 1, 1, 0, 0, 0, 0, 0},
        {0, 1, 1, 1, 1, 0, 0, 0},
        {1, 0, 0, 1, 1, 1, 1, 1},
    }};
};

} // namespace mapperbus::core
