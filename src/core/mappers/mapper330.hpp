#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 330: Sangokushi II bootleg N163-like discrete board.
class Mapper330 final : public Mapper {
  public:
    Mapper330(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    bool maps_prg(Address addr) const override;
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    std::optional<Address> nametable_address(Address addr) const override;
    void power_on() override;
    void reset() override;
    bool irq_pending() const override;
    void clock_cpu_falling_edge() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    [[nodiscard]] std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    [[nodiscard]] std::uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 8192> prg_nvram_{};
    const MirrorMode initial_mirror_;
    std::array<Byte, 3> prg_banks_{};
    std::array<Byte, 8> chr_banks_{};
    std::array<Byte, 4> nametable_banks_{};
    std::uint16_t irq_counter_ = 0U;
    bool irq_pending_ = false;
    bool irq_load_pending_ = false;
};

} // namespace mapperbus::core
