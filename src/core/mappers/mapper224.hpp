#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Jncota KT-008: AA6023A/MINDKIDS with supervisor registers at $5000-$5FFF.
class Kt008 final : public Mapper {
  public:
    Kt008(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool maps_expansion(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    bool observes_ppu_address() const override;
    void observe_ppu_address(Address addr) override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;
    void reset() override;
    void power_on() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    bool gnrom_mode() const;
    bool supervisor_locked() const;
    bool low_ram_mapped(Address addr) const;
    Byte mmc3_chr_bank(unsigned slot) const;
    Byte effective_chr_bank(unsigned slot) const;
    uint32_t prg_page(Address addr) const;
    std::size_t chr_offset(Address addr) const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_ram_;
    std::vector<Byte> prg_ram_;
    const std::size_t nvram_size_;
    const MirrorMode initial_mirror_;
    const bool nes2_;

    std::array<Byte, 6> supervisor_{};
    Byte bank_select_ = 0;
    std::array<Byte, 8> bank_registers_{};
    MirrorMode mirror_mode_;
    Byte irq_counter_ = 0;
    Byte irq_reload_ = 0;
    bool irq_reload_flag_ = false;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
    bool prg_ram_enabled_ = true;
    bool prg_ram_write_protected_ = false;
    Byte ppu_chr_slot_ = 0;
};

} // namespace mapperbus::core
