#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// X1-017: legacy mapper 82 dump order and mapper 552 physical PRG wiring.
class TaitoX1017 final : public Mapper {
  public:
    TaitoX1017(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte cpu_open_bus_mask() const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void clock_cpu() override;
    bool irq_pending() const override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    [[nodiscard]] std::size_t chr_offset(Address addr) const;
    [[nodiscard]] Byte decode_prg(Byte value) const;
    [[nodiscard]] uint16_t irq_reload(bool acknowledge) const;
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x1400> internal_ram_{};
    const bool physical_prg_;
    const bool battery_;
    const Byte initial_mirror_;
    std::array<Byte, 3> prg_banks_{};
    std::array<Byte, 6> chr_banks_{};
    Byte chr_mode_ = 0U;
    std::array<Byte, 3> ram_enabled_{};
    Byte irq_latch_ = 0U;
    Byte irq_control_ = 0U;
    Byte irq_pending_ = 0U;
    uint16_t irq_counter_ = 0U;
};

} // namespace mapperbus::core
