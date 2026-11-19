#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 298, NTDEC UNL-TF1201 (VRC4b clone without expansion audio).
class UnlTf1201 final : public Mapper {
  public:
    UnlTf1201(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);

    [[nodiscard]] static Result<void> validate_geometry(const INesHeader& header,
                                                        std::size_t prg_rom_size,
                                                        std::size_t chr_rom_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void power_on() override;
    void reset() override;
    void clock_cpu() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    enum class RamPopulation : std::uint8_t { None, Volatile, NonVolatile };

    [[nodiscard]] static Byte register_index(Address addr);
    [[nodiscard]] std::size_t prg_bank_for_slot(unsigned slot) const;
    [[nodiscard]] std::size_t chr_offset(Address addr) const;
    void write_irq_control(Byte value);
    void clock_irq();
    [[nodiscard]] RamPopulation ram_population() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 2048> prg_ram_{};
    const bool has_prg_ram_;
    const bool has_prg_nvram_;
    const MirrorMode initial_mirror_;
    std::array<Byte, 2> prg_banks_{};
    std::array<std::uint16_t, 8> chr_banks_{};
    MirrorMode mirror_;
    Byte mode_ = 0;
    Byte irq_latch_ = 0;
    Byte irq_counter_ = 0;
    std::uint16_t irq_prescaler_ = 341;
    Byte irq_control_ = 0;
    bool irq_pending_ = false;
};

} // namespace mapperbus::core
