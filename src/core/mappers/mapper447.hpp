#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 447: KL-06 VRC4 multicart with lockable NROM overrides.
class Mapper447 final : public Mapper {
  public:
    Mapper447(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;

    void power_on() override;
    void reset() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_cpu() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    struct Registers {
        Byte outer = 0U;
        std::array<Byte, 2> prg_banks{0U, 1U};
        std::array<std::uint16_t, 8> chr_banks{};
        MirrorMode mirror = MirrorMode::Vertical;
        Byte mode = 0U;
        Byte irq_latch = 0U;
        Byte irq_counter = 0U;
        std::uint16_t irq_prescaler = 341U;
        Byte irq_control = 0U;
        bool irq_enabled = false;
        bool irq_pending = false;
    };

    [[nodiscard]] unsigned inner_prg_bank(Address addr) const;
    [[nodiscard]] std::size_t prg_offset(Address addr) const;
    [[nodiscard]] std::size_t chr_offset(Address addr) const;
    void write_vrc4(Address addr, Byte value);
    void write_irq_control(Byte value);
    void tick_irq();
    [[nodiscard]] std::uint16_t memory_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x0800> prg_ram_{};
    std::size_t prg_ram_size_ = 0U;
    std::size_t nvram_size_ = 0U;
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    Registers registers_{};
};

} // namespace mapperbus::core
