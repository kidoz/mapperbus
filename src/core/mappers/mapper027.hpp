#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Unlicensed VRC4f wiring: CPU A0/A1 select registers within an $F00F mask.
class Mapper027 final : public Mapper {
  public:
    Mapper027(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    bool maps_prg(Address addr) const override;
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
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
    void write_irq_control(Byte value);
    void acknowledge_irq_register();
    void tick_irq();

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::vector<Byte> prg_ram_;
    std::array<Byte, 8192> chr_ram_{};
    const std::size_t nvram_size_;
    const MirrorMode initial_mirror_;
    std::array<Byte, 2> prg_banks_{};
    std::array<std::uint16_t, 8> chr_banks_{};
    MirrorMode mirror_;
    Byte prg_control_ = 0;
    Byte irq_latch_ = 0;
    Byte irq_counter_ = 0;
    std::uint16_t irq_prescaler_ = 341;
    Byte irq_control_ = 0;
    Byte irq_pending_ = 0;
};

} // namespace mapperbus::core
