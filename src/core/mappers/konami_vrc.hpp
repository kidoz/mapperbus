#pragma once

#include <array>
#include <optional>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// VRC1/2/3/4 boards (21/22/23/25/73/75), with chip-specific capabilities
/// and explicit NES 2.0 address wiring, plus Waixing mixed-CHR mappers 252/253.
/// No expansion sound hardware.
class KonamiVrc final : public Mapper {
  public:
    KonamiVrc(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
    Result<void> configure_board_profile(std::string_view profile) override;
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void clock_cpu() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void reset() override;
    void power_on() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    std::optional<std::size_t> waixing_chr_ram_offset(Address addr) const;
    uint16_t waixing_state_tag() const;
    void save_waixing_state(StateWriter& writer) const;
    void load_waixing_state(StateReader& reader);
    Byte port(Address addr) const;
    std::size_t chr_offset(Address addr) const;
    void irq_control(Byte value);
    void irq_acknowledge();
    void irq_tick();
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::vector<Byte> prg_ram_;
    std::array<Byte, 0x2000> chr_ram_{};
    const uint16_t mapper_;
    const uint8_t submapper_;
    const bool vrc2_;
    const std::size_t nvram_size_;
    const MirrorMode initial_mirror_;
    std::array<Byte, 3> prg_banks_{};
    std::array<uint16_t, 8> chr_banks_{};
    MirrorMode mirror_;
    bool waixing_dynamic_ = false;  // Immutable mapper-253 board profile.
    Byte waixing_chr_mode_ = 0xC8U; // Fixed or PPU-write-selected RAM decoder.
    Byte mode_ = 0;
    Byte microwire_ = 0;
    uint16_t irq_latch_ = 0;
    uint16_t irq_counter_ = 0;
    uint16_t irq_prescaler_ = 341;
    Byte irq_control_ = 0;
    Byte irq_enabled_ = 0;
    Byte irq_pending_ = 0;
};

} // namespace mapperbus::core
